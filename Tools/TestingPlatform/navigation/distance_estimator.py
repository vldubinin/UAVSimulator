from __future__ import annotations

import math

import numpy as np

from constants import DEFAULT_KALMAN_Q_VARIANCE
from state import BBox

# ── Adaptive R tuning ────────────────────────────────────────────────────────
# R = _R_BASE * (_R_REF_DIAG / diagonal_px)^2
# At diagonal = _R_REF_DIAG  → R = _R_BASE  (high confidence, target is close)
# At diagonal = 20 px        → R grows ×25  (distrust tiny bounding boxes)
_R_BASE:     float = 0.5    # measurement noise at reference distance
_R_REF_DIAG: float = 100.0  # reference diagonal in pixels


class DistanceEstimator:
    """
    Monocular distance estimator with 1D adaptive Kalman filter.

    State vector  x = [distance_m, velocity_m_per_s]  (2 × 1)
    Observation   z = raw_distance_m                  (scalar)
    Motion model  Constant Velocity (CV)
    """

    def __init__(self, focal_length_px: float, q_variance: float = DEFAULT_KALMAN_Q_VARIANCE) -> None:
        self._focal_length = focal_length_px
        self._q_variance   = q_variance

        # State [position, velocity], column vector
        self._x: np.ndarray = np.zeros((2, 1), dtype=np.float64)

        # State covariance — start with high uncertainty
        self._P: np.ndarray = np.array(
            [[500.0,  0.0],
             [  0.0, 50.0]], dtype=np.float64
        )

        # Measurement matrix H: we observe position only
        self._H: np.ndarray = np.array([[1.0, 0.0]], dtype=np.float64)

        self._I2: np.ndarray   = np.eye(2, dtype=np.float64)
        self._initialized: bool = False

    # ── Public API ─────────────────────────────────────────────────────────────

    def update_q(self, q_variance: float) -> None:
        """Dynamically adjust Q matrix intensity (called when drone class changes)."""
        self._q_variance = q_variance

    def reset(self) -> None:
        """Clear filter state after a hard tracker recovery (position discontinuity)."""
        self._x[:] = 0.0
        self._P[:] = np.array([[500.0, 0.0], [0.0, 50.0]], dtype=np.float64)
        self._initialized = False

    def calculate(self, bbox: BBox, size_mm: float, dt: float) -> float:
        """
        Returns Kalman-filtered distance in metres.

        bbox    — current bounding box (x, y, w, h) in pixels
        size_mm — known physical size of the target (diagonal equivalent, mm)
        dt      — seconds elapsed since the last call
        """
        raw_m = self._raw_distance(bbox, size_mm)
        R     = self._adaptive_R(bbox)
        F     = self._build_F(dt)
        Q     = self._build_Q(dt)

        if not self._initialized:
            self._x[0, 0]    = raw_m
            self._initialized = True

        # ── Predict ───────────────────────────────────────────────────────────
        x_pred: np.ndarray = F @ self._x
        P_pred: np.ndarray = F @ self._P @ F.T + Q

        # ── Update ────────────────────────────────────────────────────────────
        z   = np.array([[raw_m]], dtype=np.float64)   # measurement
        y   = z - self._H @ x_pred                    # innovation (scalar)
        S   = float((self._H @ P_pred @ self._H.T)[0, 0]) + R  # innovation covariance
        K   = (P_pred @ self._H.T) / S                # Kalman gain (2 × 1)

        self._x = x_pred + K * float(y[0, 0])
        self._P = (self._I2 - K @ self._H) @ P_pred

        return float(self._x[0, 0])

    # ── Private helpers ────────────────────────────────────────────────────────

    def _raw_distance(self, bbox: BBox, size_mm: float) -> float:
        """Monocular formula: distance_m = (F_px * size_mm) / (diag_px * 1000)."""
        _, _, w, h     = bbox
        diag_px        = max(math.hypot(w, h), 1.0)   # guard against zero
        return (self._focal_length * size_mm) / (diag_px * 1000.0)

    @staticmethod
    def _adaptive_R(bbox: BBox) -> float:
        """
        Measurement noise scales with inverse square of pixel diagonal.
        Small target (far away) → high R → filter trusts model over measurement.
        Large target (close)    → low  R → filter trusts measurement.
        """
        _, _, w, h = bbox
        diag_px    = max(math.hypot(w, h), 1.0)
        return _R_BASE * (_R_REF_DIAG / diag_px) ** 2

    def _build_F(self, dt: float) -> np.ndarray:
        """State transition matrix for Constant Velocity model."""
        return np.array(
            [[1.0, dt ],
             [0.0, 1.0]], dtype=np.float64
        )

    def _build_Q(self, dt: float) -> np.ndarray:
        """
        Discrete-time process noise matrix (white noise acceleration model).
        Q = q * [[dt⁴/4, dt³/2],
                 [dt³/2, dt²  ]]
        """
        dt2, dt3, dt4 = dt**2, dt**3, dt**4
        return self._q_variance * np.array(
            [[dt4 / 4.0, dt3 / 2.0],
             [dt3 / 2.0, dt2      ]], dtype=np.float64
        )
