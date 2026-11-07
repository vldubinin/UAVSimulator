"""
Observation — те, що отримує алгоритм керування на кожному кроці.

  obs.t          — симуляційний час (с) останнього пакета шини
  obs.truth      — Truth (ground truth з топіка flight_truth) або None, якщо use_truth: false
  obs.sensors    — {topic: розібраний payload}: JSON-топіки -> dict, camera/segmentation_mask -> bytes (JPEG)
  obs.camera     — декодований кадр камери (numpy BGR) або None; потребує opencv-python
  obs.get(topic) — те саме, що obs.sensors.get(topic)

Формати payload'ів сенсорів — Docs/04-SensorBus.md; flight_truth — Docs/14-Flight-Test-Lab.md.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any, Optional

BINARY_TOPICS = {"camera", "segmentation_mask"}


def parse_payload(topic: str, payload: bytes) -> Any:
    if topic in BINARY_TOPICS:
        return payload
    try:
        return json.loads(payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return payload


def _get(d: dict, *path, default=None):
    for key in path:
        if not isinstance(d, dict) or key not in d:
            return default
        d = d[key]
    return d


@dataclass
class Truth:
    """Зручний доступ до flight_truth. Усі поля — з `raw` (повний словник payload)."""
    raw: dict

    # Позиція
    @property
    def lat(self) -> Optional[float]: return _get(self.raw, "geo", "latitude")
    @property
    def lon(self) -> Optional[float]: return _get(self.raw, "geo", "longitude")
    @property
    def alt_m(self) -> Optional[float]: return _get(self.raw, "geo", "altitude_m")
    @property
    def position_m(self) -> tuple[float, float, float]:
        p = self.raw.get("position_m", {})
        return p.get("x", 0.0), p.get("y", 0.0), p.get("z", 0.0)

    # Орієнтація
    @property
    def roll_deg(self) -> float: return _get(self.raw, "attitude_deg", "roll", default=0.0)
    @property
    def pitch_deg(self) -> float: return _get(self.raw, "attitude_deg", "pitch", default=0.0)
    @property
    def yaw_deg(self) -> float: return _get(self.raw, "attitude_deg", "yaw", default=0.0)
    @property
    def heading_deg(self) -> Optional[float]: return self.raw.get("heading_deg")
    @property
    def course_deg(self) -> Optional[float]: return self.raw.get("course_deg")
    @property
    def yaw_rate_dps(self) -> float: return self.raw.get("yaw_rate_world_dps", 0.0)

    # Швидкості / аеродинаміка
    @property
    def airspeed_mps(self) -> float: return self.raw.get("airspeed_mps", 0.0)
    @property
    def ground_speed_mps(self) -> Optional[float]: return self.raw.get("ground_speed_mps")
    @property
    def vertical_speed_mps(self) -> Optional[float]: return self.raw.get("vertical_speed_mps")
    @property
    def aoa_deg(self) -> float: return self.raw.get("aoa_deg", 0.0)
    @property
    def throttle(self) -> float: return self.raw.get("throttle", 0.0)

    def __getitem__(self, key):
        return self.raw[key]


@dataclass
class Observation:
    t: float
    wall_time: float
    sensors: dict[str, Any] = field(default_factory=dict)
    truth: Optional[Truth] = None
    sensor_times: dict[str, float] = field(default_factory=dict)
    _camera_cache: Any = field(default=None, repr=False)

    def get(self, topic: str, default=None):
        return self.sensors.get(topic, default)

    @property
    def camera(self):
        """Декодований кадр (BGR numpy) з топіка camera або None."""
        if self._camera_cache is None and "camera" in self.sensors:
            try:
                import cv2
                import numpy as np
            except ImportError as exc:  # pragma: no cover
                raise RuntimeError("obs.camera потребує opencv-python: pip install opencv-python") from exc
            self._camera_cache = cv2.imdecode(np.frombuffer(self.sensors["camera"], np.uint8), cv2.IMREAD_COLOR)
        return self._camera_cache
