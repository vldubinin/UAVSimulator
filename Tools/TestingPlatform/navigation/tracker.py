from __future__ import annotations

from typing import Tuple

import cv2
import numpy as np

from state import BBox


def _make_csrt() -> cv2.Tracker:
    if hasattr(cv2, "TrackerCSRT_create"):          # OpenCV < 4.5
        return cv2.TrackerCSRT_create()
    if hasattr(cv2, "legacy"):                       # OpenCV >= 4.5 + contrib
        return cv2.legacy.TrackerCSRT_create()
    raise RuntimeError(
        "TrackerCSRT не знайдено. Виконайте:\n"
        "  pip uninstall opencv-python opencv-contrib-python -y\n"
        "  pip install opencv-contrib-python"
    )


class DroneTracker:
    """Thin wrapper around cv2.TrackerCSRT for frame-by-frame target following."""

    def __init__(self) -> None:
        self._tracker: cv2.Tracker | None = None

    def init(self, frame: np.ndarray, bbox: BBox) -> None:
        self._tracker = _make_csrt()
        self._tracker.init(frame, bbox)

    def update(self, frame: np.ndarray) -> Tuple[bool, BBox]:
        if self._tracker is None:
            return False, (0, 0, 0, 0)

        success, raw_bbox = self._tracker.update(frame)
        if not success:
            return False, (0, 0, 0, 0)

        x, y, w, h = (int(v) for v in raw_bbox)
        return True, (x, y, w, h)
