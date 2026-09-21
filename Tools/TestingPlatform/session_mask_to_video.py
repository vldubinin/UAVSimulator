"""
Renders the segmentation-mask frames of a session recorded by record_bbox_frames.py into an MP4 video.

Reads <session>/labels/*.json and takes the frames whose label has a "mask" path
(<session>/masks/NNNNNN.jpg, recorded when SegmentationMaskCB was enabled), in frame order.
No bboxes are drawn — this is the plain mask stream.

Run without arguments — the newest session in <this script's folder>/bbox_recordings is used,
the video is written to <session>/video_mask.mp4. Settings are the constants below.

FPS is derived from the recorded mask timestamps (median frame interval, world time),
falls back to DEFAULT_FPS if the timestamps are unusable (set FIXED_FPS to override).

Requires: pip install opencv-python
"""

import glob
import json
import logging
import os
import statistics
import sys

import cv2

# ─── Settings ────────────────────────────────────────────────────────────────
RECORDINGS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bbox_recordings")
OUTPUT_NAME    = "video_mask.mp4"
FIXED_FPS      = None     # e.g. 30.0 to ignore the recorded timestamps
DEFAULT_FPS    = 30.0     # used when FIXED_FPS is None and timestamps are unusable
FOURCC         = "mp4v"   # "avc1" for H.264 if your OpenCV build supports it
DRAW_INFO      = False    # overlay frame id

log = logging.getLogger("mask_video")


def find_latest_session() -> str | None:
    sessions = sorted(d for d in glob.glob(os.path.join(RECORDINGS_DIR, "session_*")) if os.path.isdir(d))
    return sessions[-1] if sessions else None  # names carry a timestamp, so the last one is the newest


def load_mask_labels(session_dir: str) -> list[dict]:
    labels = []
    for path in sorted(glob.glob(os.path.join(session_dir, "labels", "*.json"))):
        try:
            with open(path, "r", encoding="utf-8") as f:
                label = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            log.warning("cannot read %s: %s", path, e)
            continue
        if label.get("mask"):
            labels.append(label)
    labels.sort(key=lambda l: l.get("frame_id", 0))
    return labels


def estimate_fps(labels: list[dict]) -> float:
    ts = [l["mask_timestamp"] for l in labels if l.get("mask_timestamp")]
    deltas = [b - a for a, b in zip(ts, ts[1:]) if b > a]
    if not deltas:
        return DEFAULT_FPS
    fps = 1.0 / statistics.median(deltas)
    return fps if 1.0 <= fps <= 240.0 else DEFAULT_FPS


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-7s %(message)s", datefmt="%H:%M:%S")

    session_dir = find_latest_session()
    if session_dir is None:
        log.error("no sessions found in %s (run record_bbox_frames.py first)", RECORDINGS_DIR)
        return 1
    log.info("session: %s", session_dir)

    labels = load_mask_labels(session_dir)
    if not labels:
        log.error("this session has no recorded masks: enable SegmentationMaskCB in the Synthetic Data menu "
                  "(topic 'segmentation_mask') and record again")
        return 1

    fps = FIXED_FPS if FIXED_FPS else estimate_fps(labels)
    output = os.path.join(session_dir, OUTPUT_NAME)
    log.info("mask frames: %d, fps: %.2f (%s), output: %s", len(labels), fps,
             "fixed" if FIXED_FPS else "from timestamps", output)

    writer = None
    size = None
    written = skipped = 0
    for label in labels:
        img_path = os.path.join(session_dir, label["mask"])
        frame = cv2.imread(img_path, cv2.IMREAD_COLOR)
        if frame is None:
            log.warning("cannot read mask %s, skipped", img_path)
            skipped += 1
            continue

        if size is None:
            size = (frame.shape[1], frame.shape[0])
            writer = cv2.VideoWriter(output, cv2.VideoWriter_fourcc(*FOURCC), fps, size)
            if not writer.isOpened():
                log.error("cannot open VideoWriter (codec '%s', size %s)", FOURCC, size)
                return 1
        elif (frame.shape[1], frame.shape[0]) != size:
            frame = cv2.resize(frame, size, interpolation=cv2.INTER_NEAREST)  # nearest: do not blend class colors

        if DRAW_INFO:
            cv2.putText(frame, f"#{label.get('frame_id')}", (8, 20),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2, cv2.LINE_AA)

        writer.write(frame)
        written += 1
        if written % 100 == 0:
            log.info("written %d/%d", written, len(labels))

    if writer is None:
        log.error("nothing to write (masks skipped: %d)", skipped)
        return 1
    writer.release()
    log.info("done: %d frames written, %d skipped -> %s", written, skipped, output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
