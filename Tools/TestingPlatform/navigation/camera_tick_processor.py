from __future__ import annotations

import logging
import time
from concurrent.futures import Future, ThreadPoolExecutor
from typing import Optional, Protocol, Tuple

import numpy as np

from constants import (
    DEFAULT_KALMAN_Q_VARIANCE,
    IPC_DISPATCHER_INTERVAL_FRAMES,
    KNOWN_DRONES_DB,
    REDETECT_INTERVAL_FRAMES,
)
from state import BBox, TargetState, TargetStatus

logger = logging.getLogger(__name__)


# ── Dependency-Injection contracts ────────────────────────────────────────────
# camera_tick_processor knows ONLY these interfaces — never concrete classes.

_IOU_REINIT_THRESHOLD:    float = 0.5   # IoU above this → drift fix (CSRT + YOLO agree)
_CONF_RECOVERY_THRESHOLD: float = 0.65  # YOLO conf above this → force recovery

_EMA_ALPHA:          float = 0.3   # EMA smoothing factor for crop ROI (lower = smoother)
_CROP_MARGIN_FACTOR: float = 0.15  # extra margin around smoothed bbox sent to classifier


class IDetector(Protocol):
    def detect(self, frame: np.ndarray) -> Optional[tuple[BBox, float]]: ...


class ITracker(Protocol):
    def init(self, frame: np.ndarray, bbox: BBox) -> None: ...
    def update(self, frame: np.ndarray) -> Tuple[bool, BBox]: ...


class IDistanceEstimator(Protocol):
    def calculate(self, bbox: BBox, size_mm: float, dt: float) -> float: ...
    def update_q(self, q_variance: float) -> None: ...
    def reset(self) -> None: ...


class IIPC(Protocol):
    def send_nowait(self, frame: np.ndarray) -> bool: ...
    def try_recv(self) -> Optional[dict]: ...


# ── Orchestrator ──────────────────────────────────────────────────────────────

class CameraTickProcessor:
    """
    State machine that runs on every camera frame.
    Target wall-clock budget per tick: < 30 ms.
    """

    def __init__(
        self,
        detector:           IDetector,
        tracker:            ITracker,
        distance_estimator: IDistanceEstimator,
        ipc:                Optional[IIPC] = None,
    ) -> None:
        self._detector           = detector
        self._tracker            = tracker
        self._distance_estimator = distance_estimator
        self._ipc                   = ipc
        self._state                 = TargetState()
        self._frame_counter: int    = 0
        self._last_tick_time: float = time.monotonic()

        # Background thread for periodic YOLO redetection (max 1 concurrent call).
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="yolo_redetect")
        self._pending_detection: Optional[Future]       = None
        self._frame_at_dispatch: Optional[np.ndarray]  = None  # frame sent to YOLO
        self._bbox_at_dispatch:  Optional[BBox]         = None  # CSRT bbox at dispatch time

        # EMA-smoothed bbox used exclusively for classification crop generation.
        # Stored as (center_x, center_y, w, h) floats; None until first tracking frame.
        self._ema_cx: Optional[float] = None
        self._ema_cy: Optional[float] = None
        self._ema_w:  Optional[float] = None
        self._ema_h:  Optional[float] = None

        # Seed the filter with the default drone profile so Q is never uninitialised.
        self._distance_estimator.update_q(DEFAULT_KALMAN_Q_VARIANCE)

    # ── Public API ────────────────────────────────────────────────────────────

    @property
    def state(self) -> TargetState:
        return self._state

    def close(self) -> None:
        """Shut down the background YOLO thread. Call once on process exit."""
        self._executor.shutdown(wait=False, cancel_futures=True)

    def tick(self, frame: np.ndarray) -> TargetState:
        self._frame_counter += 1

        now = time.monotonic()
        dt  = now - self._last_tick_time
        self._last_tick_time = now

        # ── 1. Check IPC channel for fresh classification results (non-blocking)
        if self._ipc is not None:
            drone_info: Optional[dict] = self._ipc.try_recv()
            if drone_info is not None:
                drone_class = drone_info.get("drone_class", "")
                db_entry    = KNOWN_DRONES_DB.get(drone_class)

                if db_entry is not None:
                    self._state.physical_size_mm  = db_entry["size_mm"]
                    self._state.kalman_q_variance = db_entry["kalman_q_variance"]
                    self._distance_estimator.update_q(db_entry["kalman_q_variance"])
                    logger.info(
                        "Q-factor updated for target class %s  "
                        "(size=%.0f mm, Q=%.2f)",
                        drone_class,
                        db_entry["size_mm"],
                        db_entry["kalman_q_variance"],
                    )
                else:
                    logger.warning(
                        "Unknown drone class '%s' received from worker — "
                        "keeping current profile (size=%.0f mm, Q=%.2f)",
                        drone_class,
                        self._state.physical_size_mm,
                        self._state.kalman_q_variance,
                    )

        # ── 2. SEARCHING / LOST: run detector ─────────────────────────────────
        if self._state.status in (TargetStatus.SEARCHING, TargetStatus.LOST):
            # Clear any stale async state from the previous tracking session.
            self._pending_detection = None
            self._frame_at_dispatch = None
            self._bbox_at_dispatch  = None

            detection = self._detector.detect(frame)

            if detection is None:
                logger.debug("No target found  [%s]", self._state.status.name)
                return self._state

            bbox, _ = detection
            logger.info(
                "Target acquired bbox=%s  [%s → TRACKING]",
                bbox, self._state.status.name,
            )
            self._state.bbox   = bbox
            self._state.status = TargetStatus.TRACKING
            self._tracker.init(frame, bbox)
            self._reset_ema(bbox)  # seed EMA so the first crop is correct

        # ── 3. TRACKING: update math tracker ─────────────────────────────────
        else:
            # Apply async redetection result if the background thread finished.
            # Must run before tracker.update so a recovery-triggered LOST
            # transition can skip the rest of the TRACKING branch cleanly.
            if self._pending_detection is not None and self._pending_detection.done():
                self._consume_redetection_result()

            if self._state.status is not TargetStatus.TRACKING:
                # _consume_redetection_result transitioned us to LOST for recovery.
                return self._state

            success, bbox = self._tracker.update(frame)

            if not success:
                logger.warning("Tracker confidence drop  [TRACKING → LOST]")
                self._state.reset_to_lost()
                return self._state

            self._state.bbox = bbox
            self._update_ema(bbox)

            # Submit a new async redetection if none is already in flight.
            if (self._frame_counter % REDETECT_INTERVAL_FRAMES == 0
                    and self._pending_detection is None):
                self._frame_at_dispatch = frame.copy()
                self._bbox_at_dispatch  = bbox
                self._pending_detection = self._executor.submit(
                    self._detector.detect, self._frame_at_dispatch
                )
                logger.debug("Async redetection submitted (frame %d)", self._frame_counter)

        # ── 4. Distance estimation (runs only when we have a valid bbox) ──────
        assert self._state.bbox is not None  # flow guarantees this
        self._state.estimated_distance_m = self._distance_estimator.calculate(
            self._state.bbox,
            self._state.physical_size_mm,
            dt,
        )

        # ── 5. IPC dispatch: send crop to classifier every N frames ───────────
        if self._ipc is not None and self._frame_counter % IPC_DISPATCHER_INTERVAL_FRAMES == 0:
            crop = self._extract_crop(frame)
            self._ipc.send_nowait(crop)

        return self._state

    # ── Private helpers ───────────────────────────────────────────────────────

    def _reset_ema(self, bbox: BBox) -> None:
        """Seed EMA state directly from bbox — avoids slow convergence at session start."""
        x, y, w, h = bbox
        self._ema_cx = x + w / 2.0
        self._ema_cy = y + h / 2.0
        self._ema_w  = float(w)
        self._ema_h  = float(h)

    def _update_ema(self, bbox: BBox) -> None:
        """Blend bbox into the EMA state. Call every tracking frame."""
        x, y, w, h = bbox
        cx = x + w / 2.0
        cy = y + h / 2.0
        a  = _EMA_ALPHA
        self._ema_cx = a * cx + (1.0 - a) * self._ema_cx
        self._ema_cy = a * cy + (1.0 - a) * self._ema_cy
        self._ema_w  = a * w  + (1.0 - a) * self._ema_w
        self._ema_h  = a * h  + (1.0 - a) * self._ema_h

    def _extract_crop(self, frame: np.ndarray) -> np.ndarray:
        """Crop a margin-padded ROI from the EMA-smoothed bbox for classification."""
        img_h, img_w = frame.shape[:2]
        margin_x = self._ema_w * _CROP_MARGIN_FACTOR
        margin_y = self._ema_h * _CROP_MARGIN_FACTOR
        x1 = max(0,      int(self._ema_cx - self._ema_w / 2.0 - margin_x))
        y1 = max(0,      int(self._ema_cy - self._ema_h / 2.0 - margin_y))
        x2 = min(img_w,  int(self._ema_cx + self._ema_w / 2.0 + margin_x))
        y2 = min(img_h,  int(self._ema_cy + self._ema_h / 2.0 + margin_y))
        return frame[y1:y2, x1:x2]

    def _consume_redetection_result(self) -> None:
        """
        Read the completed Future and apply drift-fix or recovery.
        Called on the main thread only — no locking needed.
        """
        # Snapshot dispatch context before clearing it in finally.
        bbox_at_dispatch  = self._bbox_at_dispatch
        frame_at_dispatch = self._frame_at_dispatch

        try:
            result = self._pending_detection.result()
        except Exception as exc:
            logger.error("Async redetection raised an exception: %s", exc)
            result = None
        finally:
            self._pending_detection = None
            self._frame_at_dispatch = None
            self._bbox_at_dispatch  = None

        if result is None or bbox_at_dispatch is None:
            return

        yolo_bbox, yolo_conf = result
        # IoU is computed against the bbox at dispatch time — both are from the
        # same frame, so the comparison is temporally consistent.
        iou = self._iou(bbox_at_dispatch, yolo_bbox)

        if iou > _IOU_REINIT_THRESHOLD:
            # CSRT and YOLO agree — reinit on the dispatched frame to fix drift.
            self._tracker.init(frame_at_dispatch, yolo_bbox)
            self._state.bbox = yolo_bbox
            self._update_ema(yolo_bbox)
            logger.debug(
                "Async drift correction (IoU=%.2f): %s → %s",
                iou, bbox_at_dispatch, yolo_bbox,
            )
        elif yolo_conf >= _CONF_RECOVERY_THRESHOLD:
            # CSRT has drifted far. Transition to LOST so the very next tick
            # runs a fresh synchronous YOLO on the most current frame.
            self._state.reset_to_lost()
            self._distance_estimator.reset()
            logger.warning(
                "Async recovery: CSRT drifted (IoU=%.2f), YOLO conf=%.2f "
                "— transitioning to LOST for fresh detection",
                iou, yolo_conf,
            )

    @staticmethod
    def _iou(a: BBox, b: BBox) -> float:
        """Intersection over Union for two (x, y, w, h) bounding boxes."""
        ax1, ay1, aw, ah = a
        bx1, by1, bw, bh = b
        ax2, ay2 = ax1 + aw, ay1 + ah
        bx2, by2 = bx1 + bw, by1 + bh

        ix1, iy1 = max(ax1, bx1), max(ay1, by1)
        ix2, iy2 = min(ax2, bx2), min(ay2, by2)

        if ix2 <= ix1 or iy2 <= iy1:
            return 0.0

        intersection = (ix2 - ix1) * (iy2 - iy1)
        union = aw * ah + bw * bh - intersection
        return intersection / union if union > 0 else 0.0
