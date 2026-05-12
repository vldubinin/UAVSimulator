import cv2
import zmq
import json
import numpy as np
import struct
import os

# ─────────────────────────────────────────────────────────────────────────────
# ZMQ
# ─────────────────────────────────────────────────────────────────────────────

context = zmq.Context()
socket  = context.socket(zmq.SUB)
socket.setsockopt(zmq.RCVHWM, 2)
socket.connect("tcp://127.0.0.1:5555")
socket.setsockopt_string(zmq.SUBSCRIBE, "")

print("Saving frames and bboxes. Press Ctrl+C to stop.\n")

# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

def decode_camera_payload(raw: bytes):
    """Returns (bbox_metadata: dict, image: np.ndarray | None)."""
    if not isinstance(raw, (bytes, bytearray)):
        raw = bytes(raw)
    json_len = struct.unpack('i', raw[:4])[0]
    metadata = json.loads(raw[4 : 4 + json_len].decode('utf-8'))
    image    = cv2.imdecode(np.frombuffer(raw[4 + json_len:], np.uint8), cv2.IMREAD_COLOR)
    return metadata, image


def save_frame(actor_name: str, frame: np.ndarray, idx: int, bbox: dict):
    min_pt = bbox.get("Min", {})
    max_pt = bbox.get("Max", {})
    if not ("X" in min_pt and "Y" in min_pt and "X" in max_pt and "Y" in max_pt):
        return

    out_dir = os.path.join("result", actor_name)
    os.makedirs(out_dir, exist_ok=True)

    row = {str(idx): {"x_min": int(min_pt["X"]), "y_min": int(min_pt["Y"]),
                       "x_max": int(max_pt["X"]), "y_max": int(max_pt["Y"])}}

    with open(os.path.join(out_dir, "data.json"), "a", encoding="utf-8") as f:
        f.write(json.dumps(row) + "\n")

    cv2.imwrite(os.path.join(out_dir, f"{idx}.png"), frame)

    vis_dir = os.path.join(out_dir, "vis")
    os.makedirs(vis_dir, exist_ok=True)
    vis = frame.copy()
    cv2.rectangle(vis, (int(min_pt["X"]), int(min_pt["Y"])),
                       (int(max_pt["X"]), int(max_pt["Y"])), (0, 255, 0), 2)
    cv2.imwrite(os.path.join(vis_dir, f"{idx}.png"), vis)

# ─────────────────────────────────────────────────────────────────────────────
# Main loop
# ─────────────────────────────────────────────────────────────────────────────

frame_counters: dict[str, int] = {}

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
            print(f"[bus] envelope error: {e}")
            continue

        for i, sensor_info in enumerate(envelope.get("sensors", [])):
            if sensor_info.get("topic") != "camera":
                continue
            if i + 1 >= len(parts):
                break

            meta, frame = decode_camera_payload(parts[i + 1])
            if frame is None:
                continue

            for actor_name, bbox in meta.items():
                if not bbox.get("bIsValid"):
                    continue
                idx = frame_counters.get(actor_name, 0)
                save_frame(actor_name, frame, idx, bbox)
                frame_counters[actor_name] = idx + 1
                print(f"[{actor_name}] frame {idx} saved")

except KeyboardInterrupt:
    print("\nStopped.")
finally:
    socket.close()
    context.term()
