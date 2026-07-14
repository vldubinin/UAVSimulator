"""
Non-interactive counterpart to visualize_cesium_objects.py: renders the exact
same overlay (marker + smoothed label per visible landmark) onto every frame
saved by record_cesium_objects.py, but instead of showing it in a window,
encodes the sequence straight into an MP4 video file.

Reads (all produced by record_cesium_objects.py):
  frames/frame_NNNNNN_<sim_time>.jpg
  landmarks.csv     - id,x,y,z        (x=latitude, y=longitude, z=altitude)
  observations.csv  - frame,landmark_id,pixel_x,pixel_y,visible

Writes:
  cesium_objects_visualization.mp4

Timing: same idea as visualize_cesium_objects.py's autoplay — each source
frame's on-screen duration is the real sim_time gap to the next frame, scaled
by PLAYBACK_SPEED and clamped to [MIN_DELAY_MS, MAX_DELAY_MS], so the video
reproduces the same effective playback speed. Since an MP4 needs a constant
frame rate, each rendered frame is written OUTPUT_FPS-quantized: it's repeated
round(duration_ms / (1000 / OUTPUT_FPS)) times (at least once) instead of
being shown for a variable delay.

Run record_cesium_objects.py first to (re)generate frames/landmarks.csv/observations.csv.
"""

import os
import sys

import cv2

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

from visualize_cesium_objects import (
    BASE_DIR,
    MAX_DELAY_MS,
    MIN_DELAY_MS,
    PLAYBACK_SPEED,
    load_landmarks,
    load_observations,
    load_frames,
    preload_frames,
)

OUTPUT_VIDEO_FILE = os.path.join(BASE_DIR, "cesium_objects_visualization.mp4")

# Constant frame rate the MP4 is encoded at. Source frame durations (see
# module docstring) are quantized to the nearest multiple of 1000/OUTPUT_FPS
# by repeating frames, so pick something that divides typical delays cleanly.
OUTPUT_FPS = 30

FOURCC = cv2.VideoWriter_fourcc(*"mp4v")


def main():
    landmarks = load_landmarks()
    observations_by_frame = load_observations()
    frames = load_frames()
    rendered_frames = preload_frames(frames, observations_by_frame, landmarks)
    n = len(frames)

    height, width = rendered_frames[0].shape[:2]
    writer = cv2.VideoWriter(OUTPUT_VIDEO_FILE, FOURCC, OUTPUT_FPS, (width, height))
    if not writer.isOpened():
        raise RuntimeError(f"could not open video writer for {OUTPUT_VIDEO_FILE}")

    frame_duration_ms = 1000.0 / OUTPUT_FPS
    written = 0

    try:
        for i in range(n):
            sim_time = frames[i][2]
            next_sim_time = frames[(i + 1) % n][2] if i + 1 < n else sim_time
            if i + 1 < n:
                target_delay_ms = (next_sim_time - sim_time) * 1000 / PLAYBACK_SPEED
                delay_ms = max(MIN_DELAY_MS, min(MAX_DELAY_MS, target_delay_ms))
            else:
                delay_ms = frame_duration_ms  # last frame: hold for one output frame

            repeat_count = max(1, round(delay_ms / frame_duration_ms))
            for _ in range(repeat_count):
                writer.write(rendered_frames[i])
                written += 1

            if (i + 1) % 50 == 0 or i + 1 == n:
                print(f"Encoding frames... {i + 1}/{n} (video frames written: {written})", flush=True)
    finally:
        writer.release()

    duration_s = written / OUTPUT_FPS
    print(f"Готово: {OUTPUT_VIDEO_FILE} ({written} кадрів відео, ~{duration_s:.1f}с при {OUTPUT_FPS} fps)")


if __name__ == "__main__":
    main()
