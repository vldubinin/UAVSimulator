import json
import logging
import struct

import cv2
import numpy as np
import zmq

from camera_tick_processor import CameraTickProcessor
from constants import FOCAL_LENGTH_PX
from detector import DroneDetector
from distance_estimator import DistanceEstimator
from ipc_communicator import IPCCommunicator
from state import TargetStatus
from tracker import DroneTracker

# ── Configuration ─────────────────────────────────────────────────────────────

# ↓ Замініть на реальний шлях до вашого .pt файлу (detector.py, рядок 1)
MODEL_PATH: str = r"detector_weights\best.pt"

CAMERA_ZMQ_ADDRESS: str = "tcp://127.0.0.1:5555"
SHOW_VIDEO: bool = True   # False для headless-режиму (без вікна)

# ── Logging ───────────────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [MAIN] %(levelname)s  %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger(__name__)

# ── Dependency Injection — збираємо всі компоненти ───────────────────────────

detector  = DroneDetector(MODEL_PATH)
tracker   = DroneTracker()
estimator = DistanceEstimator(focal_length_px=FOCAL_LENGTH_PX)
ipc       = IPCCommunicator()

processor = CameraTickProcessor(
    detector=detector,
    tracker=tracker,
    distance_estimator=estimator,
    ipc=ipc,
)

logger.info("All components initialised. Waiting for camera stream on %s", CAMERA_ZMQ_ADDRESS)

# ── ZMQ camera bus (original logic) ──────────────────────────────────────────

context = zmq.Context()
socket  = context.socket(zmq.SUB)
socket.setsockopt(zmq.RCVHWM, 2)
socket.connect(CAMERA_ZMQ_ADDRESS)
socket.setsockopt_string(zmq.SUBSCRIBE, "")

print("Processing frames from UAV`s camera. Press Ctrl+C to stop.\n")

# ── Helpers ───────────────────────────────────────────────────────────────────

def decode_camera_payload(raw: bytes):
    """Returns (metadata: dict, image: np.ndarray | None)."""
    if not isinstance(raw, (bytes, bytearray)):
        raw = bytes(raw)
    json_len = struct.unpack("i", raw[:4])[0]
    metadata = json.loads(raw[4 : 4 + json_len].decode("utf-8"))
    image    = cv2.imdecode(np.frombuffer(raw[4 + json_len:], np.uint8), cv2.IMREAD_COLOR)
    return metadata, image


_STATUS_COLOR = {
    TargetStatus.SEARCHING: (0, 165, 255),  # orange
    TargetStatus.TRACKING:  (0, 255,   0),  # green
    TargetStatus.LOST:      (0,   0, 255),  # red
}
_LIDAR_COLOR   = (0, 220, 255)   # yellow
_PANEL_BG      = (25, 25, 25)    # near-black
_LABEL_COLOR   = (140, 140, 140) # mid-gray for row labels
_HEADER_COLOR  = (200, 200, 200) # light-gray for panel title
_NO_DATA_COLOR = (80, 80, 80)    # dim-gray for "---"
_DIVIDER_COLOR = (70, 70, 70)
_PANEL_W       = 200

def _build_side_panel(height: int, state) -> np.ndarray:
    panel = np.full((height, _PANEL_W, 3), _PANEL_BG, dtype=np.uint8)

    # Left border
    cv2.line(panel, (0, 0), (0, height - 1), _DIVIDER_COLOR, 2)

    # ── Header ────────────────────────────────────────────────────────────
    cv2.putText(panel, "DISTANCES",
                (12, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.55, _HEADER_COLOR, 1, cv2.LINE_AA)
    cv2.line(panel, (10, 42), (_PANEL_W - 10, 42), _DIVIDER_COLOR, 1)

    # ── Estimated distance ────────────────────────────────────────────────
    cv2.putText(panel, "ESTIMATED",
                (12, 72), cv2.FONT_HERSHEY_SIMPLEX, 0.42, _LABEL_COLOR, 1, cv2.LINE_AA)

    est_color = _STATUS_COLOR.get(state.status, (255, 255, 255))
    if state.bbox is not None:
        est_text  = f"{state.estimated_distance_m:.1f} m"
    else:
        est_text  = "---"
        est_color = _NO_DATA_COLOR
    cv2.putText(panel, est_text,
                (12, 112), cv2.FONT_HERSHEY_SIMPLEX, 1.0, est_color, 2, cv2.LINE_AA)

    cv2.line(panel, (10, 130), (_PANEL_W - 10, 130), _DIVIDER_COLOR, 1)

    # ── Lidar distance ────────────────────────────────────────────────────
    cv2.putText(panel, "LIDAR",
                (12, 158), cv2.FONT_HERSHEY_SIMPLEX, 0.42, _LABEL_COLOR, 1, cv2.LINE_AA)

    if state.lidar_distance_m is not None:
        lidar_text  = f"{state.lidar_distance_m:.1f} m"
        lidar_color = _LIDAR_COLOR
    else:
        lidar_text  = "---"
        lidar_color = _NO_DATA_COLOR
    cv2.putText(panel, lidar_text,
                (12, 198), cv2.FONT_HERSHEY_SIMPLEX, 1.0, lidar_color, 2, cv2.LINE_AA)

    cv2.line(panel, (10, 216), (_PANEL_W - 10, 216), _DIVIDER_COLOR, 1)

    # ── Error % ───────────────────────────────────────────────────────────
    cv2.putText(panel, "ERROR",
                (12, 244), cv2.FONT_HERSHEY_SIMPLEX, 0.42, _LABEL_COLOR, 1, cv2.LINE_AA)

    if state.bbox is not None and state.lidar_distance_m:
        err_pct = abs(state.estimated_distance_m - state.lidar_distance_m) / state.lidar_distance_m * 100
        err_text  = f"{err_pct:.1f} %"
        if err_pct < 10:
            err_color = (0, 210, 0)    # green
        elif err_pct < 25:
            err_color = (0, 200, 255)  # yellow
        else:
            err_color = (0, 0, 220)    # red
    else:
        err_text  = "---"
        err_color = _NO_DATA_COLOR
    cv2.putText(panel, err_text,
                (12, 284), cv2.FONT_HERSHEY_SIMPLEX, 1.0, err_color, 2, cv2.LINE_AA)

    return panel


def draw_overlay(frame: np.ndarray, state) -> np.ndarray:
    vis   = frame.copy()
    color = _STATUS_COLOR.get(state.status, (255, 255, 255))
    

    if state.bbox is not None:
        x, y, w, h = state.bbox
        cv2.rectangle(vis, (x, y), (x + w, y + h), color, 1)
        

    cv2.putText(
        vis, state.status.name,
        (10, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2, cv2.LINE_AA,
    )

    panel = _build_side_panel(vis.shape[0], state)
    return np.hstack([vis, panel])

# ── Main loop ─────────────────────────────────────────────────────────────────

try:
    while True:
        if not socket.poll(timeout=1):
            continue

        parts = socket.recv_multipart()
        if len(parts) < 2:
            continue

        try:
            envelope = json.loads(parts[0].decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            logger.warning("Envelope decode error: %s", e)
            continue

        # ── Collect all sensor payloads from this multipart message ──────────
        frame      = None
        lidar_scan = None

        for i, sensor_info in enumerate(envelope.get("sensors", [])):
            if i + 1 >= len(parts):
                break
            topic = sensor_info.get("topic")
            if topic == "camera":
                _, frame = decode_camera_payload(parts[i + 1])
            elif topic == "lidar":
                try:
                    lidar_scan = json.loads(parts[i + 1].decode("utf-8"))
                except (json.JSONDecodeError, UnicodeDecodeError) as exc:
                    logger.warning("Lidar payload decode error: %s", exc)

        if frame is None:
            continue

        state = processor.tick(frame)

        # Lidar distance: minimum hit across all actors, UE cm → m
        if lidar_scan:
            min_cm = min(lidar_scan.values())
            if min_cm > 0:
                state.lidar_distance_m = min_cm / 100.0

        if SHOW_VIDEO:
            cv2.imshow("Navigation", draw_overlay(frame, state))
            if cv2.waitKey(1) & 0xFF == ord("q"):
                raise KeyboardInterrupt

except KeyboardInterrupt:
    logger.info("Stopped by user.")
finally:
    socket.close()
    context.term()
    ipc.close()
    processor.close()
    if SHOW_VIDEO:
        cv2.destroyAllWindows()
    logger.info("Shutdown complete.")
