"""
Non-interactive counterpart to export_cesium_objects_video.py: encodes the
frames saved by record_cesium_objects.py into an MP4 video *without* drawing
any marker/label overlay, and without repeating or dropping frames.

Reads (produced by record_cesium_objects.py):
  frames/frame_NNNNNN_<sim_time>.jpg

Writes:
  cesium_objects_video_plain.mp4     - encoded video, one video frame per source frame
  cesium_objects_video_plain_map.csv - video_frame_index,frame_index,filename,sim_time

Frame correspondence: unlike export_cesium_objects_video.py, this script does
NOT try to reproduce real playback speed. record_cesium_objects.py drops
warm-up/duplicate camera frames, so the real sim_time gap between consecutive
saved frames is uneven; pacing the output by that gap (as
export_cesium_objects_video.py does, repeating frames to fill variable
delays) would break the 1:1 mapping between a saved frame and a video frame.
Here every saved frame becomes *exactly one* encoded video frame, in
frame_index order, none repeated or skipped, so:

    video frame i (0-based)  <->  frames[i]  <->  frame_index = frames[i][0]

and every observations.csv row with that frame_index describes exactly video
frame i. The companion _map.csv above spells this mapping out explicitly so
it still holds even if frame_index ever isn't perfectly contiguous (e.g.
frame files removed by hand between recording and export). OUTPUT_FPS below
is therefore only the container's constant playback rate, not a
reconstruction of elapsed sim time.

Run record_cesium_objects.py first to (re)generate frames/observations.csv.
"""

import csv
import os
import sys

import cv2

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

from visualize_cesium_objects import BASE_DIR, FRAMES_DIR, load_frames

OUTPUT_VIDEO_FILE = os.path.join(BASE_DIR, "cesium_objects_video_plain.mp4")
OUTPUT_MAP_FILE   = os.path.join(BASE_DIR, "cesium_objects_video_plain_map.csv")

# Purely the container's constant playback frame rate; since every source
# frame is written exactly once (see module docstring), this has no effect on
# which video frame a given observations.csv frame_index lands on.
OUTPUT_FPS = 30

FOURCC = cv2.VideoWriter_fourcc(*"mp4v")


def main():
    frames = load_frames()
    n = len(frames)

    first_img = cv2.imread(os.path.join(FRAMES_DIR, frames[0][1]))
    if first_img is None:
        raise RuntimeError(f"could not read frame image {frames[0][1]}")
    height, width = first_img.shape[:2]

    writer = cv2.VideoWriter(OUTPUT_VIDEO_FILE, FOURCC, OUTPUT_FPS, (width, height))
    if not writer.isOpened():
        raise RuntimeError(f"could not open video writer for {OUTPUT_VIDEO_FILE}")

    try:
        with open(OUTPUT_MAP_FILE, "w", newline="", encoding="utf-8") as map_f:
            map_writer = csv.writer(map_f)
            map_writer.writerow(["video_frame_index", "frame_index", "filename", "sim_time"])

            for i, (frame_index, filename, sim_time) in enumerate(frames):
                img = first_img if i == 0 else cv2.imread(os.path.join(FRAMES_DIR, filename))
                if img is None:
                    raise RuntimeError(f"could not read frame image {filename}")
                if img.shape[:2] != (height, width):
                    raise RuntimeError(
                        f"{filename} is {img.shape[1]}x{img.shape[0]}, expected {width}x{height} "
                        f"- all frames must be the same size to keep frame numbering aligned"
                    )

                writer.write(img)
                map_writer.writerow([i, frame_index, filename, sim_time])

                if (i + 1) % 50 == 0 or i + 1 == n:
                    print(f"Encoding frames... {i + 1}/{n}", flush=True)
    finally:
        writer.release()

    duration_s = n / OUTPUT_FPS
    print(f"Готово: {OUTPUT_VIDEO_FILE} ({n} кадрів відео, ~{duration_s:.1f}с при {OUTPUT_FPS} fps)")
    print(f"Мапа video_frame_index -> frame_index: {OUTPUT_MAP_FILE}")


if __name__ == "__main__":
    main()
