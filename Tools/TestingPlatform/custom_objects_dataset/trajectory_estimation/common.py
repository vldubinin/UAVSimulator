"""
Спільний, суто допоміжний шар без бізнес-логіки: геодезичні хелпери й тип
позиції, спільні для trajectory_estimation.py та map_view.py.
"""

from __future__ import annotations

import math

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
