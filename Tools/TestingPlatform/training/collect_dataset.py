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

print("Saving frames and bboxes in YOLO format. Press Ctrl+C to stop.\n")

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

    # Отримуємо розміри зображення для нормалізації координат
    img_height, img_width = frame.shape[:2]

    # Витягуємо абсолютні координати
    x_min = float(min_pt["X"])
    y_min = float(min_pt["Y"])
    x_max = float(max_pt["X"])
    y_max = float(max_pt["Y"])

    # Обчислюємо формат YOLO (нормалізовані центр, ширина, висота)
    box_width = x_max - x_min
    box_height = y_max - y_min
    
    x_center = x_min + (box_width / 2.0)
    y_center = y_min + (box_height / 2.0)

    x_center_norm = x_center / img_width
    y_center_norm = y_center / img_height
    width_norm = box_width / img_width
    height_norm = box_height / img_height

    # Формуємо шляхи до папок (датасет та візуалізація окремо)
    dataset_dir = os.path.join("dataset", actor_name)
    images_dir = os.path.join(dataset_dir, "images")
    labels_dir = os.path.join(dataset_dir, "labels")
    vis_dir = os.path.join("vis", actor_name)

    # Створюємо директорії, якщо їх не існує
    os.makedirs(images_dir, exist_ok=True)
    os.makedirs(labels_dir, exist_ok=True)
    os.makedirs(vis_dir, exist_ok=True)

    # 1. Збереження зображення
    cv2.imwrite(os.path.join(images_dir, f"{idx}.png"), frame)

    # 2. Збереження мітки (label) у форматі YOLO
    # Припускаємо, що клас "дрон" має ідентифікатор 0
    class_id = 0
    label_path = os.path.join(labels_dir, f"{idx}.txt")
    with open(label_path, "w", encoding="utf-8") as f:
        f.write(f"{class_id} {x_center_norm:.6f} {y_center_norm:.6f} {width_norm:.6f} {height_norm:.6f}\n")

    # 3. Збереження візуалізації (за межами датасету)
    vis = frame.copy()
    cv2.rectangle(vis, (int(x_min), int(y_min)), (int(x_max), int(y_max)), (0, 255, 0), 2)
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