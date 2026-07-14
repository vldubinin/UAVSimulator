"""
Interactive viewer that overlays cesium_objects landmark detections onto the
frames saved by record_cesium_objects.py.

For the frame currently shown it draws, for every observation belonging to
that frame with visible=true, a marker at the exact (pixel_x, pixel_y)
labeled with the landmark's id (truncated), lat/lon/vis (from
landmarks.csv/observations.csv, named "lat"/"lon"/"vis" — vis is 0 or 1), and
altitude. The label text itself is anchored to a temporally-smoothed position
per landmark_id (see LABEL_SMOOTHING_ALPHA) instead of the raw per-frame
pixel — cesium_objects re-projects positions fresh every sim tick, which
makes the raw pixel jitter a little frame to frame even when nothing moved
much; smoothing only the text (not the marker dot) keeps labels readable
without hiding the marker's true position.

Reads (all produced by record_cesium_objects.py):
  frames/frame_NNNNNN_<sim_time>.jpg
  landmarks.csv     - id,x,y,z        (x=latitude, y=longitude, z=altitude)
  observations.csv  - frame,landmark_id,pixel_x,pixel_y,visible

All frames are decoded and overlaid once up front (see preload_frames()) so
autoplay never touches disk or redraws anything — the only per-step cost is
handing an already-built image to cv2.imshow(). This keeps frame-to-frame
timing consistent; the whole dataset must fit in memory, which is fine at the
scale record_cesium_objects.py produces (a few hundred MB for a few hundred
frames), but would need revisiting for a much larger recording.

Playback starts automatically. Since record_cesium_objects.py now drops
duplicate camera frames (see its docstring), consecutive saved frames can be
an uneven number of sim ticks apart — advancing at a fixed delay would make
motion look jumpier the more frames were pruned. Instead each step's delay is
the real sim_time gap between that frame and the next (parsed from the
frame_NNNNNN_<sim_time>.jpg filename), scaled by PLAYBACK_SPEED, minus
whatever time this iteration already spent (should be negligible now that
frames are preloaded, but kept for precision), and clamped to
[MIN_DELAY_MS, MAX_DELAY_MS] so playback speed reflects actual elapsed
simulation time regardless of how many frames were pruned upstream. It loops
back to the first frame after the last one. Space toggles play/pause; any
manual navigation key also pauses playback:
  Space           -> play / pause
  Right / d / n   -> next frame (pauses)
  Left  / a / p   -> previous frame (pauses)
  Home / End      -> jump to first / last frame (pauses)
  g               -> type a frame number to jump to (pauses)
  q / Esc         -> quit

Run record_cesium_objects.py first to (re)generate frames/landmarks.csv/observations.csv.
"""

import csv
import os
import re
import time
from collections import defaultdict

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

BASE_DIR          = os.path.dirname(os.path.abspath(__file__))
FRAMES_DIR        = os.path.join(BASE_DIR, "frames")
LANDMARKS_FILE    = os.path.join(BASE_DIR, "landmarks.csv")
OBSERVATIONS_FILE = os.path.join(BASE_DIR, "observations.csv")

WINDOW_NAME = "Cesium-об'єкти (space = пауза/відтворення, n/p = кадр, g = перейти, q = вихід)"
VISIBLE_COLOR   = (0, 220, 0)      # BGR

# Autoplay is paced by the real sim_time gap between consecutive saved frames,
# scaled by PLAYBACK_SPEED (1.0 = real-time, 0.5 = half speed/2x slower, 2.0 =
# double speed). Every recorded frame is still shown either way, so lowering
# this only stretches playback out in time — it stays just as smooth. Clamped
# so a huge gap (e.g. right after a long stretch of skipped warm-up/duplicate
# frames) doesn't freeze playback for seconds, and a near-zero gap doesn't
# spin faster than the display can show.
PLAYBACK_SPEED = 0.3
MIN_DELAY_MS   = 15
MAX_DELAY_MS   = 1000

FONT_PATH = "C:/Windows/Fonts/arial.ttf"
_font_cache = {}

FRAME_FILENAME_RE = re.compile(r"^frame_(\d+)_([\d.]+)\.jpg$")


def get_font(size):
    if size not in _font_cache:
        try:
            _font_cache[size] = ImageFont.truetype(FONT_PATH, size)
        except OSError:
            _font_cache[size] = ImageFont.load_default()
    return _font_cache[size]


def draw_texts(img_bgr, items):
    """items: list of (text, (x, y), color_bgr, font_size, bg)"""
    img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)
    pil_img = Image.fromarray(img_rgb)
    draw = ImageDraw.Draw(pil_img)
    for text, (x, y), color_bgr, size, bg in items:
        font = get_font(size)
        color_rgb = (color_bgr[2], color_bgr[1], color_bgr[0])
        if bg is not None:
            bbox = draw.textbbox((x, y), text, font=font)
            draw.rectangle(bbox, fill=bg)
        draw.text((x, y), text, font=font, fill=color_rgb)
    return cv2.cvtColor(np.array(pil_img), cv2.COLOR_RGB2BGR)


def load_landmarks():
    """id -> (x, y, z) = (latitude, longitude, altitude)"""
    if not os.path.exists(LANDMARKS_FILE):
        raise RuntimeError(f"{LANDMARKS_FILE} not found - run record_cesium_objects.py first.")
    landmarks = {}
    with open(LANDMARKS_FILE, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            landmarks[row["id"]] = (float(row["x"]), float(row["y"]), float(row["z"]))
    return landmarks


def load_observations():
    """frame_index (int) -> list of dicts {landmark_id, pixel_x, pixel_y, visible}"""
    if not os.path.exists(OBSERVATIONS_FILE):
        raise RuntimeError(f"{OBSERVATIONS_FILE} not found - run record_cesium_objects.py first.")
    by_frame = defaultdict(list)
    with open(OBSERVATIONS_FILE, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            by_frame[int(row["frame"])].append({
                "landmark_id": row["landmark_id"],
                "pixel_x": float(row["pixel_x"]),
                "pixel_y": float(row["pixel_y"]),
                "visible": row["visible"].strip().lower() in ("true", "1"),
            })
    return by_frame


def load_frames():
    """Sorted list of (frame_index, filename, sim_time), parsed from frame_NNNNNN_<sim_time>.jpg."""
    if not os.path.isdir(FRAMES_DIR):
        raise RuntimeError(f"{FRAMES_DIR} not found - run record_cesium_objects.py first.")
    frames = []
    for name in os.listdir(FRAMES_DIR):
        match = FRAME_FILENAME_RE.match(name)
        if match:
            frames.append((int(match.group(1)), name, float(match.group(2))))
    frames.sort(key=lambda t: t[0])
    if not frames:
        raise RuntimeError(f"No frame_*.jpg files in {FRAMES_DIR}.")
    return frames


def short_id(landmark_id, max_len=28):
    return landmark_id if len(landmark_id) <= max_len else landmark_id[:max_len - 1] + "…"


def render_frame(filename, observations, landmarks, label_positions):
    """label_positions: landmark_id -> (x, y) smoothed anchor for that id's text (see preload_frames)."""
    img = cv2.imread(os.path.join(FRAMES_DIR, filename))
    if img is None:
        raise RuntimeError(f"could not read frame image {filename}")

    text_items = []

    for obs in observations:
        if not obs["visible"]:
            continue

        pt = (int(round(obs["pixel_x"])), int(round(obs["pixel_y"])))
        cv2.drawMarker(img, pt, VISIBLE_COLOR, cv2.MARKER_CROSS, 16, 2, cv2.LINE_AA)
        cv2.circle(img, pt, 10, VISIBLE_COLOR, 2, cv2.LINE_AA)

        lat, lon, altitude = landmarks.get(obs["landmark_id"], (None, None, None))
        vis_int = 1 if obs["visible"] else 0
        alt_str = f" h={altitude:.0f}м" if altitude is not None else ""
        coord_str = f"lat={lat:.5f} lon={lon:.5f}" if lat is not None and lon is not None else ""

        label_x, label_y = label_positions.get(obs["landmark_id"], (obs["pixel_x"], obs["pixel_y"]))
        label_pt = (int(round(label_x)), int(round(label_y)))

        label_line1 = short_id(obs["landmark_id"])
        label_line2 = f"{coord_str} vis={vis_int}{alt_str}".strip()
        text_items.append((label_line1, (label_pt[0] + 12, label_pt[1] - 32), VISIBLE_COLOR, 13, (0, 0, 0)))
        text_items.append((label_line2, (label_pt[0] + 12, label_pt[1] - 16), VISIBLE_COLOR, 12, (0, 0, 0)))

    return draw_texts(img, text_items)


# How strongly each new frame's raw pixel position pulls the text label toward
# it (0-1). Lower = steadier/more readable text but lags behind fast motion
# more; higher = more responsive but jitters more. The marker dot itself is
# never smoothed — only where its text label is anchored.
LABEL_SMOOTHING_ALPHA = 0.15


def preload_frames(frames, observations_by_frame, landmarks):
    """Renders every frame once up front so autoplay only ever does an imshow()."""
    n = len(frames)
    rendered = []
    smoothed_positions = {}  # landmark_id -> (x, y), carried across frames in chronological order

    for i, (frame_index, filename, _) in enumerate(frames):
        observations = observations_by_frame.get(frame_index, [])

        label_positions = {}
        for obs in observations:
            if not obs["visible"]:
                continue
            raw = (obs["pixel_x"], obs["pixel_y"])
            prev = smoothed_positions.get(obs["landmark_id"])
            if prev is None:
                new = raw
            else:
                new = (
                    LABEL_SMOOTHING_ALPHA * raw[0] + (1 - LABEL_SMOOTHING_ALPHA) * prev[0],
                    LABEL_SMOOTHING_ALPHA * raw[1] + (1 - LABEL_SMOOTHING_ALPHA) * prev[1],
                )
            smoothed_positions[obs["landmark_id"]] = new
            label_positions[obs["landmark_id"]] = new

        rendered.append(render_frame(filename, observations, landmarks, label_positions))
        if (i + 1) % 50 == 0 or i + 1 == n:
            print(f"Rendering frames... {i + 1}/{n}", flush=True)

    return rendered


def main():
    landmarks = load_landmarks()
    observations_by_frame = load_observations()
    frames = load_frames()
    rendered_frames = preload_frames(frames, observations_by_frame, landmarks)
    idx = 0
    n = len(frames)
    playing = True

    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)

    while True:
        step_start = time.perf_counter()
        _, _, sim_time = frames[idx]
        state_icon = "▶" if playing else "⏸"
        cv2.setWindowTitle(WINDOW_NAME, f"{WINDOW_NAME}  [{idx + 1}/{n}]  {state_icon}")
        cv2.imshow(WINDOW_NAME, rendered_frames[idx])

        if playing:
            next_sim_time = frames[(idx + 1) % n][2]
            target_delay_ms = (next_sim_time - sim_time) * 1000 / PLAYBACK_SPEED
            elapsed_ms = (time.perf_counter() - step_start) * 1000
            delay_ms = int(round(target_delay_ms - elapsed_ms))
            delay_ms = max(MIN_DELAY_MS, min(MAX_DELAY_MS, delay_ms))
        else:
            delay_ms = 0

        key = cv2.waitKeyEx(delay_ms)

        if key == -1:
            # Autoplay timeout — no key was pressed, advance and loop back at the end.
            idx = (idx + 1) % n
            continue

        if key in (27, ord('q'), ord('Q')):
            break
        elif key == ord(' '):
            playing = not playing
        elif key in (2555904, 65363, ord('d'), ord('D'), ord('n'), ord('N')):  # right arrow / d / n
            playing = False
            idx = min(idx + 1, n - 1)
        elif key in (2424832, 65361, ord('a'), ord('A'), ord('p'), ord('P')):  # left arrow / a / p
            playing = False
            idx = max(idx - 1, 0)
        elif key in (2359296, 65360):  # Home
            playing = False
            idx = 0
        elif key in (2293760, 65367):  # End
            playing = False
            idx = n - 1
        elif key in (ord('g'), ord('G')):
            playing = False
            try:
                raw = input(f"Перейти до кадру (1-{n}): ")
                target = int(raw) - 1
                idx = min(max(target, 0), n - 1)
            except ValueError:
                pass

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
