"""
Records camera frames together with target bounding boxes from UAVSimulator's sensor bus.

Subscribes (ZMQ SUB) to the bus published by USensorBusComponent and, from every
multipart message, takes these sensors:

  * camera topic (default "camera_tp", fallback "camera") — JPEG bytes of the onboard camera frame;
  * bbox   topic (default "bbox")                          — UBBoxDetectionComponent JSON:
        {"<ActorName>": {"Min": {"X", "Y"}, "Max": {"X", "Y"}, "bIsValid": bool}, ...}
    (pixel coordinates of the camera render target, origin at the top-left corner);
  * mask   topic (default "segmentation_mask", OPTIONAL) — JPEG bytes of the segmentation mask,
    published when SegmentationMaskCB is enabled. Recorded automatically when it is on the bus.

All sensors of one bus tick arrive in the *same* multipart message, so the frame, the bbox
and the mask saved together are always paired by the bus itself.

Bus wire format (docs: Docs/04-SensorBus.md):
    Part 0:   JSON envelope {"timestamp": T, "sensors": [{"topic": "...", "timestamp": T1}, ...]}
    Part 1..N raw payload of each sensor, in the same order as "sensors".

Output layout (one session folder per run):
    <out-dir>/session_YYYYmmdd_HHMMSS/
        images/000001.jpg      raw JPEG bytes exactly as received (no re-encoding)
        masks/000001.jpg       segmentation mask of that frame (only if the mask topic is on the bus;
                               JPEG as published by the simulator, i.e. lossy)
        labels/000001.json     frame metadata + all bboxes of that frame (+ "mask" path or null)
        recorder.log           the same log that goes to the console

Logging: INFO by default (connection, periodic stats, every saved sample, problems);
--verbose adds DEBUG (every bus message: topics + payload sizes, skip reasons).

Also shows the live stream with the bboxes drawn on it (disable with --no-display).

Usage:
    python record_bbox_frames.py
    python record_bbox_frames.py --endpoint tcp://127.0.0.1:5555 --out-dir dataset_bbox
    python record_bbox_frames.py --camera-topic camera --no-display --verbose

Requires: pip install pyzmq opencv-python numpy
"""

import argparse
import json
import logging
import os
import sys
import time
from datetime import datetime

import cv2
import numpy as np
import zmq

RECV_TIMEOUT_MS = 100
STATS_INTERVAL_S = 3.0

COLOR_VALID = (0, 255, 0)

log = logging.getLogger("recorder")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--endpoint", default="tcp://127.0.0.1:5555", help="ZMQ PUB endpoint of USensorBusComponent")
    p.add_argument("--camera-topic", default="camera_tp,camera",
                   help="topic with JPEG camera frames; comma-separated list of names, the first one present wins "
                        "(UCameraFrameComponent publishes 'camera' by default)")
    p.add_argument("--bbox-topic", default="bbox", help="topic with UBBoxDetectionComponent JSON")
    p.add_argument("--mask-topic", default="segmentation_mask",
                   help="optional topic with the JPEG segmentation mask (SegmentationMaskCB)")
    p.add_argument("--out-dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "bbox_recordings"),
                   help="root folder for recorded sessions (default: bbox_recordings next to this script, "
                        "where session_to_video.py looks for it)")
    p.add_argument("--no-display", action="store_true", help="do not show the live window")
    p.add_argument("--save-without-bbox", action="store_true",
                   help="also save frames that came without a bbox message (saved with an empty box list)")
    p.add_argument("--only-valid", action="store_true",
                   help="save a frame only if at least one bbox is valid")
    p.add_argument("--verbose", action="store_true", help="DEBUG log: every bus message and every skip reason")
    return p.parse_args()


def setup_logging(session_dir: str, verbose: bool) -> None:
    fmt = logging.Formatter("%(asctime)s %(levelname)-7s %(message)s", datefmt="%H:%M:%S")
    log.setLevel(logging.DEBUG if verbose else logging.INFO)

    console = logging.StreamHandler(sys.stdout)
    console.setFormatter(fmt)
    log.addHandler(console)

    file_handler = logging.FileHandler(os.path.join(session_dir, "recorder.log"), encoding="utf-8")
    file_handler.setFormatter(fmt)
    log.addHandler(file_handler)


def parse_bboxes(raw: bytes) -> list[dict]:
    """Converts the bbox payload into a flat list of dicts (pixel coordinates)."""
    data = json.loads(raw.decode("utf-8"))
    boxes = []
    for name, box in data.items():
        try:
            boxes.append({
                "name":     name,
                "x_min":    float(box["Min"]["X"]),
                "y_min":    float(box["Min"]["Y"]),
                "x_max":    float(box["Max"]["X"]),
                "y_max":    float(box["Max"]["Y"]),
                "is_valid": bool(box.get("bIsValid", False)),
            })
        except (KeyError, TypeError, ValueError):
            log.warning("bbox entry '%s' has unexpected format, skipped: %r", name, box)
    return boxes


def draw_boxes(frame: np.ndarray, boxes: list[dict]) -> np.ndarray:
    vis = frame.copy()
    for b in boxes:
        if not b["is_valid"]:
            continue  # invalid box has no meaningful coordinates
        p1 = (int(round(b["x_min"])), int(round(b["y_min"])))
        p2 = (int(round(b["x_max"])), int(round(b["y_max"])))
        cv2.rectangle(vis, p1, p2, COLOR_VALID, 2)
        cv2.putText(vis, b["name"], (p1[0], max(p1[1] - 6, 12)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, COLOR_VALID, 1, cv2.LINE_AA)
    return vis


def save_sample(session_dir: str, idx: int, jpeg: bytes, frame: np.ndarray,
                boxes: list[dict], cam_ts: float, bbox_ts: float | None, bus_ts: float,
                mask_jpeg: bytes | None = None, mask_ts: float | None = None) -> None:
    images_dir = os.path.join(session_dir, "images")
    labels_dir = os.path.join(session_dir, "labels")
    name = f"{idx:06d}"

    with open(os.path.join(images_dir, f"{name}.jpg"), "wb") as f:
        f.write(jpeg)

    mask_rel = None
    if mask_jpeg is not None:
        masks_dir = os.path.join(session_dir, "masks")
        os.makedirs(masks_dir, exist_ok=True)
        with open(os.path.join(masks_dir, f"{name}.jpg"), "wb") as f:
            f.write(mask_jpeg)
        mask_rel = f"masks/{name}.jpg"

    height, width = frame.shape[:2]
    label = {
        "frame_id":         idx,
        "image":            f"images/{name}.jpg",
        "image_width":      width,
        "image_height":     height,
        "bus_timestamp":    bus_ts,
        "camera_timestamp": cam_ts,
        "bbox_timestamp":   bbox_ts,
        "mask":             mask_rel,
        "mask_timestamp":   mask_ts,
        "boxes":            boxes,
    }
    with open(os.path.join(labels_dir, f"{name}.json"), "w", encoding="utf-8") as f:
        json.dump(label, f, ensure_ascii=False, indent=2)


class Stats:
    """Counters for the periodic INFO line and for the 'why is nothing saved' diagnostics."""

    def __init__(self) -> None:
        self.messages = 0
        self.topics: dict[str, int] = {}
        self.camera_frames = 0
        self.bbox_messages = 0
        self.bbox_empty = 0      # bbox messages with no actors at all ({})
        self.bbox_nonvalid = 0   # bbox messages that had actors but none with bIsValid
        self.mask_frames = 0
        self.saved = 0
        self.skipped: dict[str, int] = {}

    def skip(self, reason: str) -> None:
        self.skipped[reason] = self.skipped.get(reason, 0) + 1
        log.debug("frame skipped: %s", reason)


def report(stats: Stats, camera_topics: list[str], bbox_topic: str, mask_topic: str) -> None:
    if stats.messages == 0:
        log.warning("no bus messages received yet: check that the simulation is running (Start pressed), "
                    "the endpoint is correct and the airplane has a USensorBusComponent with enabled sensors")
        return

    log.info("messages=%d camera_frames=%d bbox_messages=%d (empty=%d, no valid=%d) mask_frames=%d saved=%d "
             "skipped=%s topics=%s",
             stats.messages, stats.camera_frames, stats.bbox_messages, stats.bbox_empty, stats.bbox_nonvalid,
             stats.mask_frames, stats.saved, stats.skipped, stats.topics)
    if mask_topic not in stats.topics:
        log.info("mask topic '%s' is not on the bus: masks are not recorded (enable SegmentationMaskCB in the "
                 "Synthetic Data menu if you need them)", mask_topic)

    if stats.bbox_messages > 0 and stats.bbox_empty == stats.bbox_messages:
        log.warning("bbox topic arrives, but EVERY message is empty ({}): the simulator's UBBoxDetectionComponent "
                    "found no actors (its ray sweep hit nothing). This is on the simulator side, not in this script: "
                    "the target must be within BBox 'Range' (default 5000 cm = 50 m) of the camera, inside its "
                    "vertical FOV band, and its mesh must block the collision channel (default ECC_Visibility)")
    elif stats.bbox_messages > 0 and stats.bbox_empty + stats.bbox_nonvalid == stats.bbox_messages:
        log.warning("bbox actors are detected, but none has bIsValid=true (no visible projection in the frame)")

    if not any(t in stats.topics for t in camera_topics):
        log.warning("camera topic %s NOT seen on the bus. Seen topics: %s -> pass one of them via --camera-topic "
                    "(or enable the camera sensor for this airplane)", camera_topics, sorted(stats.topics))
    if bbox_topic not in stats.topics:
        log.warning("bbox topic '%s' NOT seen on the bus. Seen topics: %s -> pass the right one via --bbox-topic "
                    "(or enable BBox detection / check the sensors role mode)", bbox_topic, sorted(stats.topics))


def main() -> int:
    args = parse_args()

    session_dir = os.path.join(args.out_dir, "session_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(os.path.join(session_dir, "images"), exist_ok=True)
    os.makedirs(os.path.join(session_dir, "labels"), exist_ok=True)
    setup_logging(session_dir, args.verbose)

    camera_topics = [t.strip() for t in args.camera_topic.split(",") if t.strip()]

    ctx = zmq.Context()
    socket = ctx.socket(zmq.SUB)
    socket.setsockopt(zmq.RCVHWM, 2)
    socket.setsockopt(zmq.SUBSCRIBE, b"")
    socket.setsockopt(zmq.RCVTIMEO, RECV_TIMEOUT_MS)
    socket.connect(args.endpoint)

    log.info("SUB socket connecting to %s (ZMQ connects lazily: no error if the simulator is not up yet)", args.endpoint)
    log.info("camera topic(s): %s, bbox topic: '%s', mask topic (optional): '%s'",
             camera_topics, args.bbox_topic, args.mask_topic)
    log.info("saving to: %s", os.path.abspath(session_dir))
    log.info("press Ctrl+C%s to stop", "" if args.no_display else " or 'q' / Esc in the window")

    stats = Stats()
    last_report = time.monotonic()
    try:
        while True:
            now = time.monotonic()
            if now - last_report >= STATS_INTERVAL_S:
                last_report = now
                report(stats, camera_topics, args.bbox_topic, args.mask_topic)

            try:
                parts = socket.recv_multipart()
            except zmq.Again:
                if not args.no_display and cv2.waitKey(1) in (ord("q"), 27):
                    break
                continue

            try:
                envelope = json.loads(parts[0].decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError) as e:
                log.error("bad bus envelope: %s", e)
                continue

            sensors = envelope.get("sensors", [])
            stats.messages += 1
            if stats.messages == 1:
                log.info("first bus message received, topics: %s", [s.get("topic") for s in sensors])
            if len(parts) - 1 != len(sensors):
                log.warning("envelope lists %d sensors but message has %d payload parts", len(sensors), len(parts) - 1)
            for s in sensors:
                stats.topics[s.get("topic")] = stats.topics.get(s.get("topic"), 0) + 1
            log.debug("message #%d: %s", stats.messages,
                      {s.get("topic"): len(parts[i]) for i, s in enumerate(sensors, start=1) if i < len(parts)})

            jpeg = None
            cam_priority = 0
            cam_ts = 0.0
            boxes = None
            bbox_ts = None
            mask_jpeg = None
            mask_ts = None

            for i, sensor in enumerate(sensors, start=1):
                if i >= len(parts):
                    break
                topic = sensor.get("topic")
                if topic in camera_topics:
                    if jpeg is not None and camera_topics.index(topic) > cam_priority:
                        continue
                    jpeg = bytes(parts[i])
                    cam_priority = camera_topics.index(topic)
                    cam_ts = float(sensor.get("timestamp", 0.0))
                elif topic == args.bbox_topic:
                    try:
                        boxes = parse_bboxes(parts[i])
                        bbox_ts = float(sensor.get("timestamp", 0.0))
                        stats.bbox_messages += 1
                        if not boxes:
                            stats.bbox_empty += 1
                        elif not any(b["is_valid"] for b in boxes):
                            stats.bbox_nonvalid += 1
                        log.debug("bbox raw payload: %s", bytes(parts[i]).decode("utf-8", "replace")[:300])
                    except (json.JSONDecodeError, UnicodeDecodeError) as e:
                        log.error("bbox payload is not valid JSON: %s", e)
                elif topic == args.mask_topic:
                    mask_jpeg = bytes(parts[i])
                    mask_ts = float(sensor.get("timestamp", 0.0))
                    stats.mask_frames += 1

            if jpeg is None:
                stats.skip("no camera frame in message")
                continue
            stats.camera_frames += 1

            frame = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
            if frame is None:
                log.error("failed to decode camera JPEG (%d bytes)", len(jpeg))
                stats.skip("JPEG decode failed")
                continue

            if not args.no_display:
                cv2.imshow("UAV camera + bbox", draw_boxes(frame, boxes or []))
                if mask_jpeg is not None:
                    mask_img = cv2.imdecode(np.frombuffer(mask_jpeg, np.uint8), cv2.IMREAD_COLOR)
                    if mask_img is not None:
                        cv2.imshow("UAV segmentation mask", mask_img)
                if cv2.waitKey(1) in (ord("q"), 27):
                    break

            if boxes is None and not args.save_without_bbox:
                stats.skip("camera frame without bbox message")
                continue
            boxes = boxes or []
            if args.only_valid and not any(b["is_valid"] for b in boxes):
                stats.skip("no valid bbox (--only-valid)")
                continue

            stats.saved += 1
            save_sample(session_dir, stats.saved, jpeg, frame, boxes, cam_ts, bbox_ts,
                        float(envelope.get("timestamp", 0.0)), mask_jpeg, mask_ts)
            valid = sum(1 for b in boxes if b["is_valid"])
            log.info("saved #%06d: %dx%d, %d bbox (%d valid), mask: %s", stats.saved, frame.shape[1],
                     frame.shape[0], len(boxes), valid, "yes" if mask_jpeg is not None else "no")

    except KeyboardInterrupt:
        log.info("interrupted by user")
    finally:
        report(stats, camera_topics, args.bbox_topic, args.mask_topic)
        log.info("stopped, saved samples: %d, log: %s", stats.saved, os.path.join(session_dir, "recorder.log"))
        socket.close()
        ctx.term()
        if not args.no_display:
            cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    sys.exit(main())
