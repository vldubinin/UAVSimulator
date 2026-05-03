import ctypes
import cv2
import zmq
import json
import numpy as np

# ─────────────────────────────────────────────────────────────────────────────
# Layout constants
# ─────────────────────────────────────────────────────────────────────────────

CAM_W, CAM_H   = 640, 480   # camera frame size (must match UUAVCameraComponent)
LIDAR_W        = 420        # LiDAR panel width; height is matched to CAM_H
MAX_RANGE_CM   = 5000.0
MAX_ROWS       = 18         # rows that fit in CAM_H with the chosen font size

# ─────────────────────────────────────────────────────────────────────────────
# Panel builders
# ─────────────────────────────────────────────────────────────────────────────

def _distance_color(dist_cm: float):
    """BGR color: red (close) → green (far)."""
    ratio = min(dist_cm / MAX_RANGE_CM, 1.0)
    return (0, int(ratio * 220), int((1.0 - ratio) * 220))


def build_camera_panel(frame) -> np.ndarray:
    """Returns the camera frame, or a 'waiting' placeholder at the same size."""
    if frame is not None:
        return frame
    placeholder = np.full((CAM_H, CAM_W, 3), 18, dtype=np.uint8)
    cv2.putText(placeholder, "Waiting for camera...", (140, CAM_H // 2),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (80, 80, 80), 1, cv2.LINE_AA)
    return placeholder


def build_lidar_panel(scan_results: dict) -> np.ndarray:
    panel = np.full((CAM_H, LIDAR_W, 3), 24, dtype=np.uint8)

    # Header
    cv2.putText(panel, "LiDAR", (10, 28),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 220, 220), 2, cv2.LINE_AA)
    cv2.putText(panel, f"{len(scan_results)} object(s)", (LIDAR_W - 130, 28),
                cv2.FONT_HERSHEY_SIMPLEX, 0.42, (120, 120, 120), 1, cv2.LINE_AA)
    cv2.line(panel, (0, 38), (LIDAR_W, 38), (50, 50, 50), 1)

    if not scan_results:
        cv2.putText(panel, "No objects detected", (10, 72),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.48, (70, 70, 70), 1, cv2.LINE_AA)
        return panel

    sorted_hits = sorted(scan_results.items(), key=lambda x: x[1])[:MAX_ROWS]
    bar_max_w   = LIDAR_W - 20
    y           = 62

    for name, dist_cm in sorted_hits:
        color  = _distance_color(dist_cm)
        bar_w  = max(2, int((min(dist_cm, MAX_RANGE_CM) / MAX_RANGE_CM) * bar_max_w))

        cv2.rectangle(panel, (10, y - 13), (10 + bar_max_w, y + 3), (38, 38, 38), -1)
        cv2.rectangle(panel, (10, y - 13), (10 + bar_w,     y + 3), color,        -1)

        label = f"{name[:24]}:  {dist_cm / 100:.1f} m"
        cv2.putText(panel, label, (13, y),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.40, (215, 215, 215), 1, cv2.LINE_AA)
        y += 23

    if len(scan_results) > MAX_ROWS:
        cv2.putText(panel, f"… {len(scan_results) - MAX_ROWS} more", (10, y + 8),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.38, (70, 70, 70), 1, cv2.LINE_AA)

    return panel


WINDOW_TITLE = "UAV Sensor Dashboard"


def _pin_window_topmost() -> bool:
    """Set the dashboard window as always-on-top via Win32. Returns True on success."""
    hwnd = ctypes.windll.user32.FindWindowW(None, WINDOW_TITLE)
    if not hwnd:
        return False
    HWND_TOPMOST  = -1
    SWP_NOMOVE    = 0x0002
    SWP_NOSIZE    = 0x0001
    ctypes.windll.user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)
    return True


def build_combined(camera_frame, lidar_data: dict) -> np.ndarray:
    cam   = build_camera_panel(camera_frame)
    lidar = build_lidar_panel(lidar_data)
    sep   = np.full((CAM_H, 2, 3), 45, dtype=np.uint8)   # 2 px vertical divider
    return np.hstack([cam, sep, lidar])


# ─────────────────────────────────────────────────────────────────────────────
# ZMQ setup
# ─────────────────────────────────────────────────────────────────────────────

context = zmq.Context()
socket  = context.socket(zmq.SUB)
socket.connect("tcp://127.0.0.1:5555")
socket.setsockopt_string(zmq.SUBSCRIBE, "")

print("Waiting for sensor data from Unreal Engine...")
print("Press 'q' to quit.\n")

# ─────────────────────────────────────────────────────────────────────────────
# Main loop
# ─────────────────────────────────────────────────────────────────────────────

latest_camera_frame = None
latest_lidar_data: dict = {}
_topmost_pinned = False

while True:
    if socket.poll(timeout=1):
        parts = socket.recv_multipart()

        if len(parts) < 2:
            continue

        try:
            envelope = json.loads(parts[0].decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            print(f"[bus] envelope parse error: {e}")
            continue

        for i, sensor_info in enumerate(envelope.get("sensors", [])):
            payload_index = i + 1
            if payload_index >= len(parts):
                break

            topic   = sensor_info.get("topic", "")
            payload = parts[payload_index]

            if topic == "camera":
                arr   = np.frombuffer(payload, np.uint8)
                frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
                if frame is not None:
                    latest_camera_frame = frame

            elif topic == "lidar":
                try:
                    latest_lidar_data = json.loads(payload.decode("utf-8"))
                except (json.JSONDecodeError, UnicodeDecodeError) as e:
                    print(f"[lidar] payload parse error: {e}")

    cv2.imshow(WINDOW_TITLE, build_combined(latest_camera_frame, latest_lidar_data))

    if not _topmost_pinned:
        # waitKey flushes the event queue so the window gets a valid HWND
        cv2.waitKey(1)
        _topmost_pinned = _pin_window_topmost()

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cv2.destroyAllWindows()
