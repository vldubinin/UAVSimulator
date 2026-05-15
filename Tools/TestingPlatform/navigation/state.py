from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Optional, Tuple

from constants import DEFAULT_KALMAN_Q_VARIANCE, DEFAULT_PHYSICAL_SIZE_MM


class TargetStatus(Enum):
    SEARCHING = auto()
    TRACKING  = auto()
    LOST      = auto()


BBox = Tuple[int, int, int, int]  # (x, y, w, h)


@dataclass
class TargetState:
    status:               TargetStatus  = field(default=TargetStatus.SEARCHING)
    bbox:                 Optional[BBox] = field(default=None)
    physical_size_mm:     float          = field(default=DEFAULT_PHYSICAL_SIZE_MM)
    kalman_q_variance:    float          = field(default=DEFAULT_KALMAN_Q_VARIANCE)
    estimated_distance_m: float          = field(default=0.0)
    lidar_distance_m:     Optional[float] = field(default=None)

    def reset_to_searching(self) -> None:
        self.status = TargetStatus.SEARCHING
        self.bbox   = None

    def reset_to_lost(self) -> None:
        self.status = TargetStatus.LOST
        self.bbox   = None
