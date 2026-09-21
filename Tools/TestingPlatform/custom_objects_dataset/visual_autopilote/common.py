"""
Спільний, суто допоміжний шар без бізнес-логіки: геодезичні хелпери, тип позиції
та дата-класи, якими обмінюються UavFlyTest <-> Autopilote і UavFlyTest <-> Laboratory.

Autopilote і Laboratory НЕ імпортують нічого з uav_fly_test.py (щоб уникнути
циклічного імпорту — UavFlyTest сама імпортує обидва класи), тож усе спільне
живе тут.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Optional

DEG_M = 111_320.0  # метрів на градус широти (наближення)

# (latitude, longitude, altitude_m)
Position = tuple[float, float, float]


def latlon_to_local_m(lat: float, lon: float, ref_lat: float, ref_lon: float) -> tuple[float, float]:
    east = (lon - ref_lon) * math.cos(math.radians(ref_lat)) * DEG_M
    north = (lat - ref_lat) * DEG_M
    return east, north


def local_m_to_latlon(east: float, north: float, ref_lat: float, ref_lon: float) -> tuple[float, float]:
    lat = ref_lat + north / DEG_M
    lon = ref_lon + east / (math.cos(math.radians(ref_lat)) * DEG_M)
    return lat, lon


def wrap180(angle_deg: float) -> float:
    return (angle_deg + 180.0) % 360.0 - 180.0


@dataclass
class ControlCommand:
    """Команди керування, що їх повертає Autopilote.compute_command. Поля
    dist_m/heading_deg/bearing_deg — інформаційні (для друку/CSV-логу в
    UavFlyTest), рахуються всередині Autopilote як побічний продукт його
    власної логіки, щоб UavFlyTest не дублював ту саму тригонометрію."""
    roll_deg: float
    pitch_deg: float
    yaw_rate_deg_s: float
    thrust: float
    status: str
    dist_m: Optional[float] = None
    heading_deg: Optional[float] = None
    bearing_deg: Optional[float] = None
