"""
Autopilote.

Один публічний метод, compute_command, приймає:
  - drone_position — розраховану (PnP) позицію дрона (lat, lon, alt_m), або
    None, якщо фіксу зараз немає;
  - target_position — позицію поточної цільової точки (lat, lon, alt_m).

Курс дрона зовні не передається — Autopilote сам оцінює його (throttled, як
_HeadingEstimator у uav_fly_test.py) з послідовних drone_position, тримаючи
власний внутрішній стан між викликами.

Повертає ControlCommand (common.py) — команди керування: roll_deg, pitch_deg,
yaw_rate_deg_s, thrust (0..1), і опційні інформаційні поля для логу/друку в
UavFlyTest (status, dist_m, heading_deg, bearing_deg).

Логіка керування (plan.md):
  1. Відхилення курсу від цілі 0-3° — плавне пропорційне донаведення: невеликий
     нахил, тяга 50-60%.
  2. Відхилення > 3° — крутий швидкий розворот: одразу повний нахил (60°) у
     бік цілі + тяга ~85%, без плавного "закладання дуги" — щоб повертати
     максимально вузько.
  Pitch в обох режимах в першу чергу керує висотою (пропорційно до відхилення
  від target_alt, у межах руля висоти) — і лише коли дрон уже близько до
  цільової висоти, під час крутого розвороту додається фіксований "тягнучий"
  pitch з plan.md (тягне розворот, а не висоту).

Коротка (< POSITION_LOSS_HOLD_S) втрата фіксу позиції не перериває маневр —
утримується остання команда.
"""

from __future__ import annotations

import math
import time
from typing import Optional

from common import ControlCommand, Position, latlon_to_local_m, wrap180

# Оцінка курсу з послідовних drone_position — троттлено на інтервал і мінімальне
# зміщення, щоб шум одиночного PnP-фіксу не домінував над реальним рухом.
HEADING_UPDATE_INTERVAL_S: float = 0.5
MIN_HEADING_DISPLACEMENT_M: float = 2.0
# Фізично неможливий стрибок оціненого курсу (шум позиції, а не реальний
# розворот) відкидається — лишається попередня оцінка.
MAX_HEADING_RATE_DEG_S: float = 60.0

# Поріг переходу між режимами керування (град. відхилення курсу від цілі, plan.md).
COURSE_ERROR_THRESHOLD_DEG: float = 3.0

# Межі руля висоти, plan.md.
ELEVATOR_MAX_UP_DEG: float = 28.0
ELEVATOR_MAX_DOWN_DEG: float = 23.0

# Режим 1 — політ у напрямку цілі, відхилення 0-3°: плавне пропорційне керування.
THRUST_SMOOTH_MIN: float = 0.50
THRUST_SMOOTH_MAX: float = 0.60
BANK_SMOOTH_MAX_DEG: float = 18.0  # в межах максимуму елеронів (20° вверх), plan.md

# Режим 2 — крутий швидкий розворот, відхилення > 3°: фіксовані параметри з plan.md.
BANK_STEEP_DEG: float = 50.0
THRUST_STEEP: float = 0.85
# Руль висоти +40-50% від максимуму "вверх" (28°) — не для набору висоти, а щоб
# затягнути розворот при вже виконаному нахилі (plan.md). Застосовується лише
# коли дрон уже близько до цільової висоти (ALT_ERROR_DEADBAND_M) — інакше
# пріоритет має вихід на потрібну висоту (ALT_PITCH_GAIN_DEG_PER_M нижче).
PITCH_STEEP_DEG: float = 0.45 * 28.0

# Керування висотою: pitch пропорційний відхиленню від target_alt, у межах
# руля висоти. Поза дедбендом коректування висоти важливіше за "затягування"
# розвороту (інакше дрон ніколи не спускається/не піднімається до цілі).
ALT_ERROR_DEADBAND_M: float = 15.0
ALT_PITCH_GAIN_DEG_PER_M: float = 0.5

# Короткочасна втрата фіксу позиції (< цього) не має переривати вже розпочатий
# маневр — попередня команда просто утримується до відновлення фіксу.
POSITION_LOSS_HOLD_S: float = 2.0


class Autopilote:
    def __init__(self) -> None:
        self._prev_lat: Optional[float] = None
        self._prev_lon: Optional[float] = None
        self._prev_time: Optional[float] = None
        self._heading_deg: Optional[float] = None
        self._last_fix_time: Optional[float] = None
        self._last_command: Optional[ControlCommand] = None

    def compute_command(
        self,
        drone_position: Optional[Position],
        target_position: Position,
    ) -> ControlCommand:
        now = time.monotonic()

        if drone_position is None:
            return self._on_lost_fix(now)

        lat, lon, alt = drone_position
        target_lat, target_lon, target_alt = target_position

        heading_deg = self._update_heading(lat, lon, now)

        east, north = latlon_to_local_m(target_lat, target_lon, lat, lon)
        alt_error_m = alt - target_alt  # додатне = вище цілі, потрібно знижуватись
        dist_m = math.sqrt(east ** 2 + north ** 2 + (target_alt - alt) ** 2)
        bearing_deg = math.degrees(math.atan2(east, north)) % 360.0

        if heading_deg is None:
            command = ControlCommand(
                0.0, self._altitude_pitch(alt_error_m, steep_turn=False), 0.0, THRUST_SMOOTH_MIN,
                "estimating heading — level flight",
                dist_m, None, bearing_deg,
            )
        else:
            error_deg = wrap180(bearing_deg - heading_deg)
            abs_error = abs(error_deg)
            turn_sign = 1.0 if error_deg >= 0.0 else -1.0
            steep_turn = abs_error > COURSE_ERROR_THRESHOLD_DEG
            pitch_deg = self._altitude_pitch(alt_error_m, steep_turn)

            if steep_turn:
                # Крутий швидкий розворот: одразу повний нахил у бік цілі — щоб
                # повертати максимально вузько, без плавного "закладання дуги".
                roll_deg = turn_sign * BANK_STEEP_DEG
                yaw_rate_deg_s = 0.0
                thrust = THRUST_STEEP
                status = f"steep turn onto target ({abs_error:.1f}° off course, alt err {alt_error_m:+.0f}m)"
            else:
                # Плавне донаведення: нахил і тяга пропорційні відхиленню в межах смуги.
                frac = abs_error / COURSE_ERROR_THRESHOLD_DEG
                roll_deg = turn_sign * BANK_SMOOTH_MAX_DEG * frac
                yaw_rate_deg_s = 0.0
                thrust = THRUST_SMOOTH_MIN + (THRUST_SMOOTH_MAX - THRUST_SMOOTH_MIN) * frac
                status = f"tracking target ({abs_error:.1f}° off course, alt err {alt_error_m:+.0f}m)"

            command = ControlCommand(
                roll_deg, pitch_deg, yaw_rate_deg_s, thrust, status,
                dist_m, heading_deg, bearing_deg,
            )

        self._last_fix_time = now
        self._last_command = command
        return command

    def _on_lost_fix(self, now: float) -> ControlCommand:
        """Фіксу немає прямо зараз. Якщо він пропав щойно (< POSITION_LOSS_HOLD_S) —
        не переривати вже розпочатий маневр і утримати останню команду; інакше
        (тривала втрата) — безпечний рівний політ."""
        if self._last_command is not None and self._last_fix_time is not None \
                and now - self._last_fix_time < POSITION_LOSS_HOLD_S:
            held = self._last_command
            return ControlCommand(
                held.roll_deg, held.pitch_deg, held.yaw_rate_deg_s, held.thrust,
                f"no fix — holding last maneuver ({now - self._last_fix_time:.1f}s)",
                held.dist_m, held.heading_deg, held.bearing_deg,
            )
        return ControlCommand(
            0.0, 0.0, 0.0, THRUST_SMOOTH_MIN,
            "no position fix — level flight",
        )

    def _altitude_pitch(self, alt_error_m: float, steep_turn: bool) -> float:
        """Pitch керується головним чином відхиленням від target_alt (проп.
        контроль, у межах руля висоти). Лише коли дрон і так уже близько до
        цільової висоти, під час крутого розвороту використовується фіксований
        "тягнучий" pitch з plan.md (тягне розворот, а не висоту) — щоб не
        ігнорувати реальну потребу знизитись/піднятись, як це було раніше."""
        if steep_turn and abs(alt_error_m) <= ALT_ERROR_DEADBAND_M:
            return PITCH_STEEP_DEG
        pitch = -ALT_PITCH_GAIN_DEG_PER_M * alt_error_m
        return max(-ELEVATOR_MAX_DOWN_DEG, min(ELEVATOR_MAX_UP_DEG, pitch))

    def _update_heading(self, lat: float, lon: float, now: float) -> Optional[float]:
        if self._prev_time is None:
            self._prev_lat, self._prev_lon, self._prev_time = lat, lon, now
            return self._heading_deg

        elapsed = now - self._prev_time
        if elapsed < HEADING_UPDATE_INTERVAL_S:
            return self._heading_deg

        east, north = latlon_to_local_m(lat, lon, self._prev_lat, self._prev_lon)
        if math.hypot(east, north) >= MIN_HEADING_DISPLACEMENT_M:
            candidate_deg = math.degrees(math.atan2(east, north)) % 360.0
            if self._heading_deg is None:
                self._heading_deg = candidate_deg
            else:
                # Відкидаємо стрибок курсу, що вимагав би нефізичної швидкості
                # розвороту, — це шум оцінки позиції, а не реальний маневр.
                implied_rate = abs(wrap180(candidate_deg - self._heading_deg)) / elapsed
                if implied_rate <= MAX_HEADING_RATE_DEG_S:
                    self._heading_deg = candidate_deg
        self._prev_lat, self._prev_lon, self._prev_time = lat, lon, now
        return self._heading_deg
