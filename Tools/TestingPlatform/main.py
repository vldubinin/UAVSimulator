import cv2
import zmq
import json
import numpy as np

# ─────────────────────────────────────────────────────────────────────────────
# LiDAR panel renderer
# ─────────────────────────────────────────────────────────────────────────────

_PANEL_W = 420
_PANEL_H = 520
_MAX_RANGE_CM = 5000.0
_MAX_ROWS = 20


def _distance_color(dist_cm: float):
    """BGR color: red (close) → green (far) relative to _MAX_RANGE_CM."""
    ratio = min(dist_cm / _MAX_RANGE_CM, 1.0)
    return (0, int(ratio * 220), int((1.0 - ratio) * 220))


def render_lidar_panel(scan_results: dict) -> np.ndarray:
    panel = np.full((_PANEL_H, _PANEL_W, 3), 18, dtype=np.uint8)

    # Header
    cv2.putText(panel, "UAV LiDAR", (10, 28),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 220, 220), 2, cv2.LINE_AA)
    count_label = f"{len(scan_results)} object(s)"
    cv2.putText(panel, count_label, (_PANEL_W - 140, 28),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, (140, 140, 140), 1, cv2.LINE_AA)
    cv2.line(panel, (0, 38), (_PANEL_W, 38), (50, 50, 50), 1)

    if not scan_results:
        cv2.putText(panel, "No objects detected", (10, 75),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (80, 80, 80), 1, cv2.LINE_AA)
        return panel

    # Sort closest first, cap rows
    sorted_hits = sorted(scan_results.items(), key=lambda x: x[1])[:_MAX_ROWS]

    bar_max_w = _PANEL_W - 22
    y = 62
    for name, dist_cm in sorted_hits:
        dist_m = dist_cm / 100.0
        color = _distance_color(dist_cm)

        # Distance bar (background track)
        cv2.rectangle(panel, (10, y - 13), (10 + bar_max_w, y + 3), (35, 35, 35), -1)
        bar_w = max(2, int((min(dist_cm, _MAX_RANGE_CM) / _MAX_RANGE_CM) * bar_max_w))
        cv2.rectangle(panel, (10, y - 13), (10 + bar_w, y + 3), color, -1)

        # Label
        label = f"{name[:28]}:  {dist_m:.1f} m"
        cv2.putText(panel, label, (13, y),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.42, (220, 220, 220), 1, cv2.LINE_AA)

        y += 24

    if len(scan_results) > _MAX_ROWS:
        cv2.putText(panel, f"... {len(scan_results) - _MAX_ROWS} more", (10, y + 6),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, (80, 80, 80), 1, cv2.LINE_AA)

    return panel


# ─────────────────────────────────────────────────────────────────────────────
# ZMQ setup
# ─────────────────────────────────────────────────────────────────────────────

context = zmq.Context()
socket = context.socket(zmq.SUB)
socket.connect("tcp://127.0.0.1:5555")
socket.setsockopt_string(zmq.SUBSCRIBE, "")  # receive all topics

print("Waiting for sensor data from Unreal Engine...")
print("  camera → JPEG frames")
print("  lidar  → JSON distance map")
print("Press 'q' to quit.\n")

# ─────────────────────────────────────────────────────────────────────────────
# Main loop
# ─────────────────────────────────────────────────────────────────────────────

latest_camera_frame = None
latest_lidar_data: dict = {}

while True:
    # Poll up to 1 ms so the display loop stays responsive regardless of sensor rate
    if socket.poll(timeout=1):
        parts = socket.recv_multipart()
        if len(parts) != 2:
            continue

        topic = parts[0].decode("utf-8", errors="replace")
        payload = parts[1]

        if topic == "camera":
            arr = np.frombuffer(payload, np.uint8)
            frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
            if frame is not None:
                latest_camera_frame = frame

        elif topic == "lidar":
            try:
                latest_lidar_data = json.loads(payload.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError) as e:
                print(f"[lidar] parse error: {e}")

    if latest_camera_frame is not None:
        cv2.imshow("UAV Camera", latest_camera_frame)

    cv2.imshow("UAV LiDAR", render_lidar_panel(latest_lidar_data))

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cv2.destroyAllWindows()
