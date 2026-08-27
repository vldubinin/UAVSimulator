"""
Ручний тест-клієнт для UAttitudeControlComponent (ZMQ PULL, tcp://*:5556).

Читає стан клавіш цифрової клавіатури (Numpad 8/2/1/3/7/9/+/-/0, утримання,
не поодинокі натискання) і безперервно шле команди SET_ATTITUDE_TARGET по
ZMQ PUSH, щоб вручну "поганяти" PID-регулятор + динаміку приводу керма
(Рівень 2+ автопілота) без написання окремого guidance-алгоритму.

Навмисно НЕ використовує W/S/A/D/стрілки — саме ці клавіші вже прив'язані
до РУЧНОГО керування літаком у Config/DefaultInput.ini (ControlW/S/A/D,
ControlUp/Down/Left/Right). Якщо вікно Unreal матиме фокус одночасно з цим
скриптом, однакові клавіші дублювали б і ручне керування, і автопілот —
Numpad-розкладка цього уникає.

Крен навмисно на Numpad1/Numpad3 (не Numpad4/Numpad6) — середній ряд
Numpad (4/5/6) виявився апаратно нерухомим на тестовій клавіатурі
(debug_numpad_keys.py підтвердив: жодна з трьох клавіш не реагує, а
сусідні 7/8/9/2 працюють — типова ознака мертвого ряду матриці).

Керування (Numpad):
    Numpad8 / Numpad2  — тангаж (pitch) вгору / вниз
    Numpad1 / Numpad3  — крен (roll) ліво / право
    Numpad7 / Numpad9  — швидкість рискання (yaw rate) ліво / право
    Numpad+ / Numpad-  — тяга (throttle) більше / менше
    Numpad0            — миттєво центрувати roll/pitch/yaw_rate (тяга не чіпається)
    Esc / Ctrl+C        — вихід

Roll/pitch/yaw_rate — самоцентрувальні (як пружинний стік): відпустив
клавішу — ціль плавно повертається до 0. Throttle — "важіль", тримає
останнє значення.

Потребує Windows (ctypes + user32.GetAsyncKeyState для читання стану
клавіш без фокусу консолі) — узгоджено з тим, що весь проєкт збирається
під Win64. Активуйте режим симуляції PlaybackAndAutoTrack — це єдиний
режим, де UAttitudeControlComponent зараз реально створюється
(AUAVSimulatorGameModeBase::StartSimulation).

Встановлення:
    pip install -r requirements.txt

Запуск:
    python keyboard_attitude_test_client.py
    python keyboard_attitude_test_client.py --endpoint tcp://127.0.0.1:5556 --rate 30
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import sys
import time
from dataclasses import dataclass

import zmq

# --- Віртуальні коди клавіш Windows (WinUser.h) ---
# Цифрова клавіатура (Numpad) — свідомо не WASD/стрілки, щоб не колізувати
# з ручним керуванням літаком (Config/DefaultInput.ini: ControlW/S/A/D, ControlUp/Down/Left/Right).
VK_NUMPAD8    = 0x68  # тангаж вгору
VK_NUMPAD2    = 0x62  # тангаж вниз
VK_NUMPAD1    = 0x61  # крен ліво (замість апаратно нерухомого Numpad4)
VK_NUMPAD3    = 0x63  # крен право (замість апаратно нерухомого Numpad6)
VK_NUMPAD7    = 0x67  # рискання ліво
VK_NUMPAD9    = 0x69  # рискання право
VK_ADD        = 0x6B  # Numpad + — тяга більше
VK_SUBTRACT   = 0x6D  # Numpad - — тяга менше
VK_NUMPAD0    = 0x60  # центрувати roll/pitch/yaw_rate (замість апаратно нерухомого Numpad5)
VK_ESCAPE     = 0x1B
VK_NUMLOCK    = 0x90

_user32 = ctypes.windll.user32


def is_key_down(vk_code: int) -> bool:
    """Стан фізичної клавіші зараз (незалежно від фокуса вікна)."""
    return bool(_user32.GetAsyncKeyState(vk_code) & 0x8000)


def is_num_lock_on() -> bool:
    """При вимкненому NumLock фізичні клавіші Numpad шлють VK_UP/VK_DOWN/... замість
    VK_NUMPAD*, тобто знову колізують зі стрілками — саме те, чого ми уникаємо."""
    return bool(_user32.GetKeyState(VK_NUMLOCK) & 0x0001)


def move_toward(current: float, target: float, max_delta: float) -> float:
    """Той самий прийом, що й FActuatorDynamics::RateLimitedOnly в C++ — крок, обмежений швидкістю."""
    delta = target - current
    if abs(delta) <= max_delta:
        return target
    return current + math.copysign(max_delta, delta)


@dataclass
class AttitudeTargetState:
    """Ціль автопілота, яку формує тест-клієнт (кути в градусах, тяга 0..1)."""

    roll_deg: float = 0.0
    pitch_deg: float = 0.0
    yaw_rate_deg_s: float = 0.0
    throttle: float = 0.0

    max_roll_deg: float = 30.0
    max_pitch_deg: float = 20.0
    max_yaw_rate_deg_s: float = 45.0

    roll_rate_deg_s: float = 45.0       # швидкість зміни цілі крену під час утримання клавіші
    pitch_rate_deg_s: float = 30.0
    yaw_rate_rate_deg_s2: float = 60.0
    center_rate_deg_s: float = 40.0     # швидкість самоцентрування при відпущеній клавіші
    throttle_step_per_sec: float = 0.6

    def update(self, dt: float) -> None:
        # Крен: Numpad3 -> право (додатний), Numpad1 -> ліво (від'ємний), інакше центрування до 0.
        if is_key_down(VK_NUMPAD3) and not is_key_down(VK_NUMPAD1):
            self.roll_deg = move_toward(self.roll_deg, self.max_roll_deg, self.roll_rate_deg_s * dt)
        elif is_key_down(VK_NUMPAD1) and not is_key_down(VK_NUMPAD3):
            self.roll_deg = move_toward(self.roll_deg, -self.max_roll_deg, self.roll_rate_deg_s * dt)
        else:
            self.roll_deg = move_toward(self.roll_deg, 0.0, self.center_rate_deg_s * dt)

        # Тангаж: Numpad8 -> кабрування (додатний), Numpad2 -> пікірування (від'ємний), інакше центрування.
        if is_key_down(VK_NUMPAD8) and not is_key_down(VK_NUMPAD2):
            self.pitch_deg = move_toward(self.pitch_deg, self.max_pitch_deg, self.pitch_rate_deg_s * dt)
        elif is_key_down(VK_NUMPAD2) and not is_key_down(VK_NUMPAD8):
            self.pitch_deg = move_toward(self.pitch_deg, -self.max_pitch_deg, self.pitch_rate_deg_s * dt)
        else:
            self.pitch_deg = move_toward(self.pitch_deg, 0.0, self.center_rate_deg_s * dt)

        # Рискання: Numpad9 -> додатне, Numpad7 -> від'ємне, інакше центрування (зупинка розвороту).
        if is_key_down(VK_NUMPAD9) and not is_key_down(VK_NUMPAD7):
            self.yaw_rate_deg_s = move_toward(self.yaw_rate_deg_s, self.max_yaw_rate_deg_s, self.yaw_rate_rate_deg_s2 * dt)
        elif is_key_down(VK_NUMPAD7) and not is_key_down(VK_NUMPAD9):
            self.yaw_rate_deg_s = move_toward(self.yaw_rate_deg_s, -self.max_yaw_rate_deg_s, self.yaw_rate_rate_deg_s2 * dt)
        else:
            self.yaw_rate_deg_s = move_toward(self.yaw_rate_deg_s, 0.0, self.center_rate_deg_s * dt)

        # Тяга: "важіль" — тримає значення між натисканнями, не центрується сама.
        if is_key_down(VK_ADD) and not is_key_down(VK_SUBTRACT):
            self.throttle = min(1.0, self.throttle + self.throttle_step_per_sec * dt)
        elif is_key_down(VK_SUBTRACT) and not is_key_down(VK_ADD):
            self.throttle = max(0.0, self.throttle - self.throttle_step_per_sec * dt)

        if is_key_down(VK_NUMPAD0):
            self.roll_deg = 0.0
            self.pitch_deg = 0.0
            self.yaw_rate_deg_s = 0.0

    def to_payload(self) -> dict:
        """SET_ATTITUDE_TARGET JSON — roll/pitch/yaw_rate у радіанах (AttitudeControlComponent
        порівнює їх напряму з FMath::DegreesToRadians(...) поточної орієнтації, без конверсії)."""
        return {
            "command_type": "SET_ATTITUDE_TARGET",
            "roll": math.radians(self.roll_deg),
            "pitch": math.radians(self.pitch_deg),
            "yaw_rate": math.radians(self.yaw_rate_deg_s),
            "thrust": self.throttle,
        }

    def status_line(self) -> str:
        num_lock_warning = "" if is_num_lock_on() else "  [!! NumLock ВИМКНЕНО !!]"
        return (
            f"roll={self.roll_deg:+6.1f}deg  pitch={self.pitch_deg:+6.1f}deg  "
            f"yaw_rate={self.yaw_rate_deg_s:+6.1f}deg/s  throttle={self.throttle*100:5.1f}%   "
            f"(Esc/Ctrl+C — вихід, Numpad0 — центрувати){num_lock_warning}"
        )


def run(endpoint: str, rate_hz: float, state: AttitudeTargetState) -> None:
    context = zmq.Context.instance()
    socket = context.socket(zmq.PUSH)
    socket.setsockopt(zmq.SNDHWM, 1)   # не накопичувати застарілі команди в черзі
    socket.setsockopt(zmq.LINGER, 0)
    socket.connect(endpoint)

    print(__doc__)
    print(f"Підключено PUSH -> {endpoint} (очікується, що UAttitudeControlComponent біндить PULL там же).")
    print("Тримайте вікно консолі активним не обов'язково — клавіші читаються глобально.\n")
    if not is_num_lock_on():
        print("!!! NumLock вимкнено — клавіші Numpad зараз шлють стрілки/навігацію, а не цифри.")
        print("!!! Увімкніть NumLock, інакше керування не спрацює як задумано.\n")

    dt_target = 1.0 / rate_hz
    last_time = time.perf_counter()

    try:
        while True:
            if is_key_down(VK_ESCAPE):
                break

            now = time.perf_counter()
            dt = now - last_time
            last_time = now

            state.update(dt)

            try:
                socket.send_json(state.to_payload(), flags=zmq.NOBLOCK)
            except zmq.Again:
                pass  # черга повна — просто пропускаємо цей тік, наступний надішле свіжіший стан

            # ljust — щоб коротший рядок (наприклад, коли зникає попередження про NumLock)
            # повністю перекрив залишок довшого попереднього рядка після \r.
            sys.stdout.write("\r" + state.status_line().ljust(110))
            sys.stdout.flush()

            elapsed = time.perf_counter() - now
            time.sleep(max(0.0, dt_target - elapsed))
    except KeyboardInterrupt:
        pass
    finally:
        print("\nЗупинка тест-клієнта.")
        socket.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Клавіатурний тест-клієнт для UAttitudeControlComponent (ZMQ PUSH).")
    parser.add_argument("--endpoint", default="tcp://127.0.0.1:5556", help="ZMQ-адреса PULL-сокета в Unreal (за замовчуванням tcp://127.0.0.1:5556).")
    parser.add_argument("--rate", type=float, default=30.0, help="Частота надсилання команд, Гц (за замовчуванням 30).")
    parser.add_argument("--max-roll", type=float, default=30.0, help="Максимальний кут крену, град.")
    parser.add_argument("--max-pitch", type=float, default=20.0, help="Максимальний кут тангажу, град.")
    parser.add_argument("--max-yaw-rate", type=float, default=45.0, help="Максимальна швидкість рискання, град/с.")
    return parser.parse_args()


def main() -> None:
    if sys.platform != "win32":
        print("Цей скрипт використовує ctypes+user32 (GetAsyncKeyState) і працює лише на Windows.", file=sys.stderr)
        sys.exit(1)

    args = parse_args()
    state = AttitudeTargetState(
        max_roll_deg=args.max_roll,
        max_pitch_deg=args.max_pitch,
        max_yaw_rate_deg_s=args.max_yaw_rate,
    )
    run(args.endpoint, args.rate, state)


if __name__ == "__main__":
    main()
