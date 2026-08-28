"""
Автопілот "зроби коло" для UAttitudeControlComponent.

Ідея експерименту
─────────────────
Керма приймає тільки АБСОЛЮТНІ кути (roll/pitch у світовій системі) + швидкість
рискання + тягу. Курсу/точки в протоколі немає. Тому коло робиться найпростіше:
тримати СТАЛИЙ крен — літак сам іде в безперервний віраж. Тангаж при цьому
підтримує висоту (одноконтурний PID на дані сенсора позиції), рискання = 0
(координацію дає планер + крен), тяга стала.

Скрипт паралельно:
  • шле команди SET_ATTITUDE_TARGET по ZMQ PUSH -> tcp://127.0.0.1:5556
  • підписується на сенсорну шину ZMQ SUB <- tcp://127.0.0.1:5555, читає топік
    "drone_position" ({"x_m","y_m","z_m"}), рахує шляховий курс, накопичений
    кут повороту, шляхову швидкість, вертикальну швидкість, миттєвий радіус
  • пише все у logs/circle_YYYYmmdd_HHMMSS.csv (команда + телеметрія в одному рядку)
  • наприкінці робить МНК-апроксимацію кола по точках траси (центр, радіус, RMS)

Фази: SETTLE (вирівнювання) -> TURN (віраж на 1 коло) -> RECOVER (вихід у рівний
політ) -> завершення.

────────────────────────────────────────────────────────────────────────────────
ЩО ТРЕБА ВВІМКНУТИ В СИМУЛЯТОРІ ПЕРЕД ЗАПУСКОМ
────────────────────────────────────────────────────────────────────────────────
1. Режим симуляції:  Playback and Auto Track
   (єдиний режим, де UAttitudeControlComponent реально активується — на літаку
    з тегом AutoTracker; там же піднімається і SensorBusComponent).
2. Sensors mode:      Drone
3. Увімкнений сенсор: Position  (топік "drone_position") — це ЄДИНИЙ потрібний сенсор.
   Решту (Camera Frame, Segmentation Mask, Lidar, BBox, Camera Altitude/Inclination,
   Cesium/Custom Surroundings, GeoPosition) можна вимкнути — менше трафіку на шині,
   стабільніша частота телеметрії.
4. Сценарій для Playback має бути записаний і вибраний у ScenarioSlotName, а перший
   його кадр — на безпечній висоті (віраж 20° біля землі = зачепиш рельєф).

Встановлення:
    pip install -r requirements.txt      (потрібен лише pyzmq)

Запуск:
    python circle_autopilot.py
    python circle_autopilot.py --bank-deg 25 --thrust 0.7 --turns 1.5
    python circle_autopilot.py --level-only --pitch-deg 3   (без утримання висоти — для порівняння)
    python circle_autopilot.py --target-alt 450             (спершу вийти на 450 м, тоді крутити коло)
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import signal
import sys
import threading
import time
from dataclasses import dataclass, field

import zmq

CMD_ENDPOINT_DEFAULT = "tcp://127.0.0.1:5556"   # PULL у UAttitudeControlComponent
TLM_ENDPOINT_DEFAULT = "tcp://127.0.0.1:5555"   # PUB у USensorBusComponent
POSITION_TOPIC = "drone_position"


# ─────────────────────────────────────────────────────────────────────────────
# Приймач сенсорної шини (окремий потік)
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class Telemetry:
    """Останній відомий стан літака з сенсора позиції + похідні величини."""
    lock: threading.Lock = field(default_factory=threading.Lock)

    have_fix: bool = False
    sim_time: float = 0.0            # timestamp конверта шини (секунди симуляції)
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0                   # висота, м
    msg_count: int = 0               # успішно розпарсених кадрів drone_position

    # діагностика шини
    bus_msgs: int = 0               # усього прийнято multipart-конвертів
    pos_msgs: int = 0              # з них містили топік drone_position
    topics_seen: set = field(default_factory=set)
    parse_errors: int = 0

    # похідні (рахуються в receiver-потоці)
    ground_speed: float = 0.0        # м/с, горизонтальна
    vertical_speed: float = 0.0      # м/с, dz/dt (згладжено)
    track_deg: float = 0.0           # шляховий курс, atan2(dy,dx), градуси
    turn_accum_deg: float = 0.0      # накопичений |поворот| курсу
    radius_inst: float = 0.0         # миттєвий радіус віражу = V / курс.швидкість

    def snapshot(self) -> dict:
        with self.lock:
            return dict(
                have_fix=self.have_fix, sim_time=self.sim_time,
                x=self.x, y=self.y, z=self.z, msg_count=self.msg_count,
                bus_msgs=self.bus_msgs, pos_msgs=self.pos_msgs,
                topics_seen=sorted(self.topics_seen), parse_errors=self.parse_errors,
                ground_speed=self.ground_speed, vertical_speed=self.vertical_speed,
                track_deg=self.track_deg, turn_accum_deg=self.turn_accum_deg,
                radius_inst=self.radius_inst,
            )


def _wrap180(a: float) -> float:
    return (a + 180.0) % 360.0 - 180.0


def receiver_loop(endpoint: str, tlm: Telemetry, stop: threading.Event) -> None:
    ctx = zmq.Context.instance()
    sub = ctx.socket(zmq.SUB)
    sub.setsockopt(zmq.RCVHWM, 20)
    sub.setsockopt_string(zmq.SUBSCRIBE, "")   # part 0 — це JSON-конверт, не топік-фрейм
    sub.connect(endpoint)

    prev = None   # (t, x, y, z)
    ema_gs = None
    ema_vs = None
    prev_track = None
    alpha = 0.35  # згладжування похідних

    printed_topics = False
    printed_first_fix = False

    try:
        while not stop.is_set():
            if not sub.poll(timeout=200):
                continue
            while True:
                try:
                    parts = sub.recv_multipart(flags=zmq.NOBLOCK)
                except zmq.Again:
                    break
                try:
                    if len(parts) < 1:
                        continue
                    try:
                        env = json.loads(parts[0].decode("utf-8"))
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        with tlm.lock:
                            tlm.parse_errors += 1
                        continue

                    sensors = env.get("sensors", [])
                    topics = [s.get("topic") for s in sensors]
                    with tlm.lock:
                        tlm.bus_msgs += 1
                        tlm.topics_seen.update(t_ for t_ in topics if t_)
                    if not printed_topics:
                        printed_topics = True
                        print(f"\n[шина] перший конверт: {len(parts)} частин, топіки={topics}\n")

                    pos = None
                    for i, s in enumerate(sensors):
                        if s.get("topic") == POSITION_TOPIC and i + 1 < len(parts):
                            try:
                                pos = json.loads(parts[i + 1].decode("utf-8"))
                            except (json.JSONDecodeError, UnicodeDecodeError):
                                with tlm.lock:
                                    tlm.parse_errors += 1
                                pos = None
                            break
                    if pos is None:
                        continue
                    with tlm.lock:
                        tlm.pos_msgs += 1

                    t = float(env.get("timestamp", time.time()))
                    x = float(pos["x_m"]); y = float(pos["y_m"]); z = float(pos["z_m"])
                    if not printed_first_fix:
                        printed_first_fix = True
                        print(f"\n[шина] перший drone_position: x={x:.2f} y={y:.2f} z={z:.2f} "
                              f"(сирий payload: {pos})\n")
                except Exception as exc:  # ніколи не роняємо receiver-потік
                    with tlm.lock:
                        tlm.parse_errors += 1
                    print(f"\n[шина] помилка обробки кадру: {exc!r}\n")
                    continue

                gs = vs = 0.0
                track = prev_track if prev_track is not None else 0.0
                d_accum = 0.0
                radius = 0.0
                if prev is not None:
                    dt = t - prev[0]
                    if dt > 1e-3:
                        dx = x - prev[1]; dy = y - prev[2]; dz = z - prev[3]
                        horiz = math.hypot(dx, dy)
                        gs = horiz / dt
                        vs = dz / dt
                        if horiz > 0.25:                     # курс лише коли реально рухаємось
                            track = math.degrees(math.atan2(dy, dx))
                            if prev_track is not None:
                                d = _wrap180(track - prev_track)
                                if gs > 3.0:
                                    d_accum = d
                                omega = math.radians(abs(d)) / dt   # курс.швидкість, рад/с
                                if omega > 1e-4:
                                    radius = gs / omega
                        ema_gs = gs if ema_gs is None else (1 - alpha) * ema_gs + alpha * gs
                        ema_vs = vs if ema_vs is None else (1 - alpha) * ema_vs + alpha * vs

                prev = (t, x, y, z)
                if track is not None:
                    prev_track = track

                with tlm.lock:
                    tlm.have_fix = True
                    tlm.sim_time = t
                    tlm.x, tlm.y, tlm.z = x, y, z
                    tlm.msg_count += 1
                    tlm.ground_speed = ema_gs if ema_gs is not None else gs
                    tlm.vertical_speed = ema_vs if ema_vs is not None else vs
                    tlm.track_deg = track
                    tlm.turn_accum_deg += d_accum
                    if radius > 0.0:
                        tlm.radius_inst = radius
    finally:
        sub.close(0)


# ─────────────────────────────────────────────────────────────────────────────
# Утримання висоти: одноконтурний PID -> кут тангажу
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class AltitudeHold:
    kp: float = 0.30          # град тангажу на метр помилки
    kd: float = 1.50          # град на (м/с) вертикальної швидкості (демпфування)
    ki: float = 0.03          # град на (м·с)
    pitch_min: float = -8.0
    pitch_max: float = 12.0
    integ: float = 0.0

    def update(self, target_alt: float, alt: float, vertical_speed: float, dt: float) -> float:
        err = target_alt - alt                       # +ve => нижче цілі => треба кабрувати
        raw = self.kp * err - self.kd * vertical_speed + self.integ
        out = max(self.pitch_min, min(self.pitch_max, raw))
        # анти-віндап: не інтегруємо, якщо вихід у насиченні й помилка штовхає туди ж
        if out == raw or (err > 0) != (raw > out):
            self.integ += self.ki * err * dt
            self.integ = max(self.pitch_min, min(self.pitch_max, self.integ))
        return out


# ─────────────────────────────────────────────────────────────────────────────
# МНК-апроксимація кола (метод Кåса) — без numpy
# ─────────────────────────────────────────────────────────────────────────────

def fit_circle(pts: list[tuple[float, float]]):
    n = len(pts)
    if n < 12:
        return None
    Sx = Sy = Sxx = Syy = Sxy = Sxz = Syz = Sz = 0.0
    for x, y in pts:
        zz = x * x + y * y
        Sx += x; Sy += y
        Sxx += x * x; Syy += y * y; Sxy += x * y
        Sxz += x * zz; Syz += y * zz; Sz += zz
    # [Sxx Sxy Sx][D]   [-Sxz]
    # [Sxy Syy Sy][E] = [-Syz]
    # [Sx  Sy  n ][F]   [-Sz ]
    a = [[Sxx, Sxy, Sx], [Sxy, Syy, Sy], [Sx, Sy, float(n)]]
    b = [-Sxz, -Syz, -Sz]

    def det3(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))

    D0 = det3(a)
    if abs(D0) < 1e-9:
        return None

    def repl(col):
        m = [row[:] for row in a]
        for r in range(3):
            m[r][col] = b[r]
        return m

    D = det3(repl(0)) / D0
    E = det3(repl(1)) / D0
    F = det3(repl(2)) / D0
    cx, cy = -D / 2.0, -E / 2.0
    rr = cx * cx + cy * cy - F
    if rr <= 0:
        return None
    R = math.sqrt(rr)
    rms = math.sqrt(sum((math.hypot(x - cx, y - cy) - R) ** 2 for x, y in pts) / n)
    return cx, cy, R, rms


# ─────────────────────────────────────────────────────────────────────────────
# Головний цикл
# ─────────────────────────────────────────────────────────────────────────────

def build_command(roll_deg: float, pitch_deg: float, yaw_rate_deg_s: float, thrust: float) -> dict:
    """SET_ATTITUDE_TARGET — кути в РАДІАНАХ (UAttitudeControlComponent порівнює їх
    напряму з поточною орієнтацією без конверсії)."""
    return {
        "command_type": "SET_ATTITUDE_TARGET",
        "roll": math.radians(roll_deg),
        "pitch": math.radians(pitch_deg),
        "yaw_rate": math.radians(yaw_rate_deg_s),
        "thrust": float(thrust),
    }


def run(args) -> None:
    os.makedirs(args.out, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    csv_path = os.path.join(args.out, f"circle_{stamp}.csv")

    ctx = zmq.Context.instance()
    push = ctx.socket(zmq.PUSH)
    push.setsockopt(zmq.SNDHWM, 1)      # не копити застарілі команди
    push.setsockopt(zmq.LINGER, 0)
    push.connect(args.cmd_endpoint)

    tlm = Telemetry()
    stop = threading.Event()
    rx = threading.Thread(target=receiver_loop, args=(args.tlm_endpoint, tlm, stop), daemon=True)
    rx.start()

    signal.signal(signal.SIGINT, lambda *_: stop.set())

    print(__doc__.split("Встановлення:")[0])
    print(f"Команди  PUSH -> {args.cmd_endpoint}")
    print(f"Телеметрія SUB <- {args.tlm_endpoint}  (топік '{POSITION_TOPIC}')")
    print(f"Лог: {csv_path}")
    print(f"Параметри: крен={args.bank_deg}°  тяга={args.thrust}  кіл={args.turns}  "
          f"{'РІВНИЙ ПОЛІТ (pitch=%.1f°)' % args.pitch_deg if args.level_only else 'утримання висоти'}\n")

    fields = ["t_s", "phase", "roll_cmd_deg", "pitch_cmd_deg", "yaw_rate_cmd_deg_s", "thrust_cmd",
              "have_fix", "x_m", "y_m", "z_m", "alt_err_m", "gs_mps", "vs_mps",
              "track_deg", "turn_accum_deg", "radius_inst_m"]
    csv_file = open(csv_path, "w", newline="", encoding="utf-8")
    writer = csv.DictWriter(csv_file, fieldnames=fields)
    writer.writeheader()

    althold = AltitudeHold(kp=args.kp_alt, kd=args.kd_alt, ki=args.ki_alt)
    dt_target = 1.0 / args.rate

    t0 = time.perf_counter()
    last = t0
    last_status = 0.0
    warned_no_tlm = False

    phase = "SETTLE"
    target_alt = None
    turn_start_accum = None
    recover_until = None
    turn_pts: list[tuple[float, float]] = []

    try:
        while not stop.is_set():
            now = time.perf_counter()
            dt = now - last
            last = now
            t = now - t0

            snap = tlm.snapshot()

            # ── логіка фаз ────────────────────────────────────────────────
            roll_cmd = 0.0
            yaw_rate_cmd = 0.0
            thrust_cmd = args.thrust

            if phase == "SETTLE":
                pitch_cmd = 0.0
                if t >= args.settle_sec:
                    if args.target_alt is not None:
                        target_alt = args.target_alt
                    elif snap["have_fix"]:
                        target_alt = snap["z"]
                    else:
                        target_alt = 0.0
                    with tlm.lock:
                        tlm.turn_accum_deg = 0.0
                    turn_start_accum = 0.0
                    phase = "TURN"
                    print(f"\n[t={t:5.1f}] -> TURN  (цільова висота {target_alt:.1f} м)")

            elif phase == "TURN":
                roll_cmd = args.bank_deg
                if args.level_only or not snap["have_fix"]:
                    pitch_cmd = args.pitch_deg
                else:
                    pitch_cmd = althold.update(target_alt, snap["z"], snap["vertical_speed"], dt)
                if snap["have_fix"]:
                    turn_pts.append((snap["x"], snap["y"]))
                swept = abs(snap["turn_accum_deg"] - (turn_start_accum or 0.0))
                if swept >= args.turns * 360.0 or t >= args.max_sec:
                    phase = "RECOVER"
                    recover_until = t + args.recover_sec
                    reason = "коло замкнуте" if swept >= args.turns * 360.0 else "ліміт часу"
                    print(f"\n[t={t:5.1f}] -> RECOVER  ({reason}, пройдено {swept:.0f}°)")

            elif phase == "RECOVER":
                roll_cmd = 0.0
                if args.level_only or not snap["have_fix"]:
                    pitch_cmd = args.pitch_deg
                else:
                    pitch_cmd = althold.update(target_alt, snap["z"], snap["vertical_speed"], dt)
                if t >= recover_until:
                    break

            # ── відправлення команди ─────────────────────────────────────
            cmd = build_command(roll_cmd, pitch_cmd, yaw_rate_cmd, thrust_cmd)
            try:
                push.send_json(cmd, flags=zmq.NOBLOCK)
            except zmq.Again:
                pass

            # ── лог у CSV ────────────────────────────────────────────────
            alt_err = (target_alt - snap["z"]) if (target_alt is not None and snap["have_fix"]) else ""
            writer.writerow({
                "t_s": f"{t:.3f}", "phase": phase,
                "roll_cmd_deg": f"{roll_cmd:.2f}", "pitch_cmd_deg": f"{pitch_cmd:.2f}",
                "yaw_rate_cmd_deg_s": f"{yaw_rate_cmd:.2f}", "thrust_cmd": f"{thrust_cmd:.3f}",
                "have_fix": int(snap["have_fix"]),
                "x_m": f"{snap['x']:.2f}", "y_m": f"{snap['y']:.2f}", "z_m": f"{snap['z']:.2f}",
                "alt_err_m": (f"{alt_err:.2f}" if alt_err != "" else ""),
                "gs_mps": f"{snap['ground_speed']:.2f}", "vs_mps": f"{snap['vertical_speed']:.2f}",
                "track_deg": f"{snap['track_deg']:.1f}", "turn_accum_deg": f"{snap['turn_accum_deg']:.1f}",
                "radius_inst_m": f"{snap['radius_inst']:.1f}",
            })
            csv_file.flush()

            # ── статус у консоль ────────────────────────────────────────
            if t - last_status >= 0.5:
                last_status = t
                if not snap["have_fix"] and t > args.settle_sec + 2.0 and not warned_no_tlm:
                    warned_no_tlm = True
                    if snap["bus_msgs"] == 0:
                        print("\n!!! Сенсорна шина не шле кадрів на "
                              f"{args.tlm_endpoint}.")
                        print("!!! Найімовірніше: НЕ ВВІМКНЕНО ЖОДНОГО СЕНСОРА. На GameMode-блупринті")
                        print("!!!   (World Settings -> GameMode) -> Details -> Simulation|Sensors:")
                        print("!!!   постав bEnableSensorPosition = true, SensorsMode = Drone,")
                        print("!!!   АБО увімкни Position у меню симулятора (розділ Sensors).")
                        print("!!! Інші причини: симуляція не в Play; режим не 'Playback and Auto Track';")
                        print("!!!   порт 5555 зайнятий попереднім PIE (закрий); інший порт (--tlm-endpoint).")
                    elif snap["pos_msgs"] == 0:
                        print(f"\n!!! Шина працює (кадрів: {snap['bus_msgs']}), але топіка "
                              f"'{POSITION_TOPIC}' у ній НЕМА.")
                        print(f"!!! Бачені топіки: {snap['topics_seen']}")
                        print("!!! Увімкни сенсор Position і постав Sensors mode = Drone у меню симулятора.")
                    else:
                        print(f"\n!!! Кадри '{POSITION_TOPIC}' приходять ({snap['pos_msgs']} шт), "
                              "але координати нульові.")
                        print("!!! Отже сенсор віддає GetActorLocation()=~0 — корінь актора не рухається "
                              "разом із фізичним мешем.")
                    print(f"!!! (parse_errors={snap['parse_errors']})\n")
                fix = "OK " if snap["have_fix"] else "-- "
                sys.stdout.write(
                    f"\r[{phase:7}] t={t:6.1f}s fix={fix}bus={snap['bus_msgs']:4d} pos={snap['pos_msgs']:4d} | "
                    f"cmd roll={roll_cmd:+5.1f} pitch={pitch_cmd:+5.1f} thr={thrust_cmd:.2f} | "
                    f"pos=({snap['x']:8.1f},{snap['y']:8.1f},{snap['z']:7.1f})  "
                    f"GS={snap['ground_speed']:5.1f} VS={snap['vertical_speed']:+5.1f}  "
                    f"track={snap['track_deg']:+6.1f} accum={snap['turn_accum_deg']:6.1f}  "
                    f"R~{snap['radius_inst']:6.1f}m".ljust(170)
                )
                sys.stdout.flush()

            sleep = dt_target - (time.perf_counter() - now)
            if sleep > 0:
                time.sleep(sleep)
    finally:
        # плавний вихід у нейтраль
        for _ in range(5):
            try:
                push.send_json(build_command(0.0, 0.0, 0.0, args.thrust), flags=zmq.NOBLOCK)
            except zmq.Again:
                pass
            time.sleep(0.02)
        stop.set()
        rx.join(timeout=1.0)
        csv_file.close()
        push.close(0)

        snap = tlm.snapshot()
        print("\n\n───────── ПІДСУМОК ─────────")
        print(f"шина: конвертів={snap['bus_msgs']}  з drone_position={snap['pos_msgs']}  "
              f"parse_errors={snap['parse_errors']}")
        print(f"топіки на шині: {snap['topics_seen']}")
        print(f"успішних фіксів позиції: {snap['msg_count']}")
        print(f"накопичений поворот курсу: {snap['turn_accum_deg']:.1f}°")
        fit = fit_circle(turn_pts)
        if fit:
            cx, cy, R, rms = fit
            print(f"апроксимація кола (фаза TURN, {len(turn_pts)} точок):")
            print(f"  центр = ({cx:.1f}, {cy:.1f}) м")
            print(f"  радіус = {R:.1f} м    RMS відхилення траси = {rms:.1f} м")
            if snap["ground_speed"] > 1:
                print(f"  очікув. час кола = {2 * math.pi * R / snap['ground_speed']:.1f} с "
                      f"при GS={snap['ground_speed']:.1f} м/с")
        else:
            print("апроксимація кола: недостатньо точок (мало телеметрії або коло не почалось)")
        print(f"CSV: {csv_path}")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Автопілот 'зроби коло' для UAttitudeControlComponent.")
    p.add_argument("--cmd-endpoint", default=CMD_ENDPOINT_DEFAULT, help="ZMQ PUSH -> PULL керми автопілота.")
    p.add_argument("--tlm-endpoint", default=TLM_ENDPOINT_DEFAULT, help="ZMQ SUB <- PUB сенсорної шини.")
    p.add_argument("--rate", type=float, default=20.0, help="Частота команд/логу, Гц.")
    p.add_argument("--bank-deg", type=float, default=20.0, help="Кут крену у віражі, град (визначає радіус).")
    p.add_argument("--thrust", type=float, default=0.65, help="Стала тяга 0..1.")
    p.add_argument("--turns", type=float, default=1.0, help="Скільки кіл пройти до фази RECOVER.")
    p.add_argument("--settle-sec", type=float, default=4.0, help="Секунд рівного польоту перед віражем.")
    p.add_argument("--recover-sec", type=float, default=4.0, help="Секунд рівного польоту після віражу.")
    p.add_argument("--max-sec", type=float, default=180.0, help="Захисний ліміт тривалості фази TURN.")
    p.add_argument("--target-alt", type=float, default=None,
                   help="Цільова висота, м. За замовч. — та, що на момент старту віражу.")
    p.add_argument("--level-only", action="store_true",
                   help="Без утримання висоти: тангаж = --pitch-deg (для порівняння open-loop).")
    p.add_argument("--pitch-deg", type=float, default=2.0, help="Сталий тангаж для --level-only / поки нема телеметрії.")
    p.add_argument("--kp-alt", type=float, default=0.30, help="PID висоти: P, град/м.")
    p.add_argument("--kd-alt", type=float, default=1.50, help="PID висоти: D по верт. швидкості, град/(м/с).")
    p.add_argument("--ki-alt", type=float, default=0.03, help="PID висоти: I, град/(м·с).")
    p.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs"),
                   help="Каталог для CSV-логів.")
    return p.parse_args()


if __name__ == "__main__":
    run(parse_args())
