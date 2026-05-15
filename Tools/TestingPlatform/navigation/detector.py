from __future__ import annotations

from typing import Optional

import numpy as np
from ultralytics import YOLO

from state import BBox


class DroneDetector:
    """Wraps a YOLOv8 .pt model; returns the highest-confidence bbox per frame."""

    def __init__(self, model_path: str, input_size: int = 320, conf_threshold: float = 0.4) -> None:
        self._model          = YOLO(model_path)
        self._input_size     = input_size
        self._conf_threshold = conf_threshold

    def detect(self, frame: np.ndarray) -> Optional[tuple[BBox, float]]:
        """Returns (bbox, confidence) for the highest-confidence detection, or None."""
        results = self._model(
            frame,
            imgsz=self._input_size,
            conf=self._conf_threshold,
            verbose=False,
        )

        best_bbox: Optional[BBox] = None
        best_conf: float          = -1.0

        for result in results:
            for box in result.boxes:
                conf: float = float(box.conf[0])
                if conf <= best_conf:
                    continue

                x1, y1, x2, y2 = (int(v) for v in box.xyxy[0])
                best_bbox = (x1, y1, x2 - x1, y2 - y1)  # → (x, y, w, h)
                best_conf = conf

        return (best_bbox, best_conf) if best_bbox is not None else None
