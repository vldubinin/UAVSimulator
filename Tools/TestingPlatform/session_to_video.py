"""
Renders a session recorded by record_bbox_frames.py into an MP4 video with the bboxes drawn on the frames.

Reads <session>/labels/*.json (frame metadata + boxes) and the matching <session>/images/*.jpg,
draws the valid bbox of TARGET_NAME only (other actors are ignored) and writes the frames in order to a video file.

Run without arguments — the newest session in <this script's folder>/bbox_recordings is used,
the video is written to <session>/video.mp4. Settings are the constants below.

FPS is derived from the recorded camera timestamps (median frame interval, world time),
falls back to DEFAULT_FPS if the timestamps are unusable (set FIXED_FPS to override).

Requires: pip install opencv-python numpy
"""

import glob
import json
import logging
import os
import statistics
import sys

import cv2
import numpy as np

# ─── Settings ────────────────────────────────────────────────────────────────
RECORDINGS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bbox_recordings")
OUTPUT_NAME    = "video.mp4"
FIXED_FPS      = None     # e.g. 30.0 to ignore the recorded timestamps
DEFAULT_FPS    = 30.0     # used when FIXED_FPS is None and timestamps are unusable
FOURCC         = "mp4v"   # "avc1" for H.264 if your OpenCV build supports it
DRAW_INFO      = True     # overlay frame id and box count
TARGET_NAME    = "Cessna_172_C_1"   # only this actor's bbox is drawn; every other actor is ignored
COLOR_VALID    = (0, 255, 0)

log = logging.getLogger("video")


def find_latest_session() -> str | None:
    sessions = sorted(d for d in glob.glob(os.path.join(RECORDINGS_DIR, "session_*")) if os.path.isdir(d))
    return sessions[-1] if sessions else None  # names carry a timestamp, so the last one is the newest


def load_labels(session_dir: str) -> list[dict]:
    paths = sorted(glob.glob(os.path.join(session_dir, "labels", "*.json")))
    labels = []
    for path in paths:
        try:
            with open(path, "r", encoding="utf-8") as f:
                labels.append(json.load(f))
        except (OSError, json.JSONDecodeError) as e:
            log.warning("cannot read %s: %s", path, e)
    labels.sort(key=lambda l: l.get("frame_id", 0))
    return labels


def estimate_fps(labels: list[dict]) -> float:
    ts = [l["camera_timestamp"] for l in labels if l.get("camera_timestamp")]
    deltas = [b - a for a, b in zip(ts, ts[1:]) if b > a]
    if not deltas:
        return DEFAULT_FPS
    fps = 1.0 / statistics.median(deltas)
    return fps if 1.0 <= fps <= 240.0 else DEFAULT_FPS


def draw_boxes(frame: np.ndarray, boxes: list[dict]) -> int:
    """Draws the valid bbox of TARGET_NAME in place; returns how many were drawn. Other actors are ignored."""
    drawn = 0
    for b in boxes:
        if b.get("name") != TARGET_NAME or not b.get("is_valid"):
            continue
        p1 = (int(round(b["x_min"])), int(round(b["y_min"])))
        p2 = (int(round(b["x_max"])), int(round(b["y_max"])))
        cv2.rectangle(frame, p1, p2, COLOR_VALID, 2)
        cv2.putText(frame, b["name"], (p1[0], max(p1[1] - 6, 12)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, COLOR_VALID, 1, cv2.LINE_AA)
        drawn += 1
    return drawn


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-7s %(message)s", datefmt="%H:%M:%S")

    session_dir = find_latest_session()
    if session_dir is None:
        log.error("no sessions found in %s (run record_bbox_frames.py first)", RECORDINGS_DIR)
        return 1
    log.info("session: %s", session_dir)

    labels = load_labels(session_dir)
    if not labels:
        log.error("no labels found in %s", os.path.join(session_dir, "labels"))
        return 1

    fps = FIXED_FPS if FIXED_FPS else estimate_fps(labels)
    output = os.path.join(session_dir, OUTPUT_NAME)
    log.info("frames: %d, fps: %.2f (%s), output: %s", len(labels), fps,
             "fixed" if FIXED_FPS else "from timestamps", output)

    writer = None
    size = None
    written = skipped = 0
    for label in labels:
        img_path = os.path.join(session_dir, label["image"])
        frame = cv2.imread(img_path, cv2.IMREAD_COLOR)
        if frame is None:
            log.warning("cannot read image %s, skipped", img_path)
            skipped += 1
            continue

        drawn = draw_boxes(frame, label.get("boxes", []))

        if size is None:
            size = (frame.shape[1], frame.shape[0])
            writer = cv2.VideoWriter(output, cv2.VideoWriter_fourcc(*FOURCC), fps, size)
            if not writer.isOpened():
                log.error("cannot open VideoWriter (codec '%s', size %s)", FOURCC, size)
                return 1
        elif (frame.shape[1], frame.shape[0]) != size:
            frame = cv2.resize(frame, size)

        if DRAW_INFO:
            cv2.putText(frame, f"#{label.get('frame_id')}  boxes: {drawn}", (8, 20),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2, cv2.LINE_AA)

        writer.write(frame)
        written += 1
        if written % 100 == 0:
            log.info("written %d/%d", written, len(labels))

    if writer is None:
        log.error("nothing to write (frames skipped: %d)", skipped)
        return 1
    writer.release()
    log.info("done: %d frames written, %d skipped -> %s", written, skipped, output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
