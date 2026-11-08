"""Критерії зупинки тесту на боці стенда (Unreal-сторона зупиняє сама: Q у меню -> session_end)."""

from __future__ import annotations

import math
from typing import Optional

from .observation import Truth

DEG_M = 111_320.0


class StopCriteria:
    def __init__(self, cfg: dict) -> None:
        self.time_limit_s = cfg.get("time_limit_s")
        self.on_crash = bool(cfg.get("on_crash", True))
        self.max_distance_m = cfg.get("max_distance_m")
        self.min_altitude_m = cfg.get("min_altitude_m")
        self.telemetry_timeout_s = cfg.get("telemetry_timeout_s") or 5.0
        self.max_step_errors = int(cfg.get("max_step_errors") or 1)
        # Зупинка без прогресу: ctx.log(progress_key, число) не зростало no_progress_s секунд
        # (напр. waypoint_index, коли літак кружляє навколо точки, яку не може взяти).
        self.progress_key = cfg.get("progress_key")
        self.no_progress_s = cfg.get("no_progress_s")
        self._best_progress: Optional[float] = None
        self._best_progress_t: Optional[float] = None
        self._start_t: Optional[float] = None
        self._start_latlon: Optional[tuple[float, float]] = None

    def check_truth(self, t: float, truth: Optional[Truth]) -> Optional[str]:
        if self._start_t is None:
            self._start_t = t
        if self.time_limit_s and t - self._start_t >= self.time_limit_s:
            return "time_limit"
        if truth is None or truth.lat is None:
            return None
        if self._start_latlon is None:
            self._start_latlon = (truth.lat, truth.lon)
        if self.max_distance_m:
            lat0, lon0 = self._start_latlon
            east = (truth.lon - lon0) * math.cos(math.radians(lat0)) * DEG_M
            north = (truth.lat - lat0) * DEG_M
            if math.hypot(east, north) > self.max_distance_m:
                return "max_distance"
        if self.min_altitude_m is not None and truth.alt_m is not None and truth.alt_m < self.min_altitude_m:
            return "min_altitude"
        return None

    def check_progress(self, t: float, logs: dict) -> Optional[str]:
        if not (self.progress_key and self.no_progress_s):
            return None
        value = logs.get(self.progress_key)
        if self._best_progress_t is None:
            self._best_progress_t = t
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if self._best_progress is None or value > self._best_progress:
                self._best_progress, self._best_progress_t = float(value), t
        if t - self._best_progress_t >= float(self.no_progress_s):
            return "no_progress"
        return None

    def elapsed(self, t: float) -> float:
        return 0.0 if self._start_t is None else t - self._start_t
