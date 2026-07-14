"""
Receives JPEG frames from UAVSimulator's CameraFrameComponent (topic "camera")
and Cesium metadata detections from CesiumSurroundingsScannerComponent (topic
"cesium_objects") over the same SensorBusComponent ZMQ PUB socket, then saves
them to OUTPUT_DIR:

  frames/frame_NNNNNN_<sim_time>.jpg  - one JPEG per camera frame
  landmarks.csv                       - id,x,y,z  (x=latitude, y=longitude, z=altitude),
                                         one row per unique object (first-seen values only)
  observations.csv                    - frame,landmark_id,pixel_x,pixel_y,visible

Observations (and new landmark rows) are only recorded when a "cesium_objects"
payload arrives in the same bus message as a "camera" frame — same rule
record_frame_and_position.py uses for drone positions — since observations.csv
references a frame index that must exist. landmarks.csv itself only ever grows
(an id already written keeps its first-seen lat/lon/alt, mirroring
CesiumSurroundingsScannerComponent's own "frozen at first-seen" ObjectStorage
semantics), and is resumed across runs by reading any ids already on disk.
frame_index also resumes across runs by continuing from the highest index
already present in FRAMES_DIR, so re-running the script never overwrites or
renumbers previous frames/observations.

The first SKIP_FIRST_FRAMES camera frames received in a run are treated as
warm-up (e.g. the sim/camera hasn't stabilized yet) and are dropped entirely:
no JPEG is written, frame_index isn't advanced, and any "cesium_objects"
payload arriving alongside a skipped frame is discarded too, so nothing about
a skipped frame ever reaches landmarks.csv/observations.csv.

UAVCameraComponent encodes JPEGs on a background thread slower than the sim
tick rate, so SensorBusComponent can publish the *same* JPEG bytes across
several consecutive bus messages while CesiumSurroundingsScannerComponent
still re-projects "cesium_objects" positions fresh every tick from the
camera's current (moving) transform. Recording every such tick as its own
frame therefore attaches several different pixel_x/pixel_y values to what is
visually one unchanged image — objects appear to drift even though nothing
moved on screen. To avoid that, a camera payload identical to the
previously-saved one is treated exactly like a warm-up frame: dropped
entirely, without advancing frame_index.

ZMQ multipart protocol (per SensorBusComponent.cpp):
  Part 0:   JSON envelope  — {"timestamp": T, "sensors": [{"topic": "...", "timestamp": T1}, ...]}
  Part 1..N: raw payloads  — one per sensor, same order as envelope["sensors"]
             ("camera" -> JPEG bytes, "cesium_objects" -> UTF8 JSON bytes:
             {"objects": [{"id","latitude","longitude","altitude","pixel_x","pixel_y","visible"}, ...]})
"""

import csv
import json
import re
import sys
from pathlib import Path

import zmq

ENDPOINT           = "tcp://localhost:5555"
OUTPUT_DIR         = Path(r"C:\Users\DubakusPC\Documents\Python\Khai\cesium_objects")
FRAMES_DIR         = OUTPUT_DIR / "frames"
LANDMARKS_FILE     = OUTPUT_DIR / "landmarks.csv"
OBSERVATIONS_FILE  = OUTPUT_DIR / "observations.csv"
RECV_TIMEOUT_MS    = 5_000

# Number of camera frames to drop as warm-up at the start of each run — skipped
# frames are not saved and never contribute to landmarks.csv/observations.csv.
SKIP_FIRST_FRAMES  = 10

FRAME_FILENAME_RE = re.compile(r"^frame_(\d+)_")


def next_frame_index() -> int:
    """Resumes numbering from the highest frame_NNNNNN_*.jpg already in FRAMES_DIR."""
    highest = -1
    for path in FRAMES_DIR.glob("frame_*.jpg"):
        match = FRAME_FILENAME_RE.match(path.name)
        if match:
            highest = max(highest, int(match.group(1)))
    return highest + 1


def load_known_landmark_ids() -> set:
    if not LANDMARKS_FILE.exists():
        return set()
    with LANDMARKS_FILE.open("r", newline="", encoding="utf-8") as f:
        return {row["id"] for row in csv.DictReader(f)}


def main() -> None:
    FRAMES_DIR.mkdir(parents=True, exist_ok=True)

    known_landmark_ids = load_known_landmark_ids()
    frame_index        = next_frame_index()

    landmarks_is_new    = not LANDMARKS_FILE.exists() or LANDMARKS_FILE.stat().st_size == 0
    observations_is_new = not OBSERVATIONS_FILE.exists() or OBSERVATIONS_FILE.stat().st_size == 0

    landmarks_out    = LANDMARKS_FILE.open("a", newline="", encoding="utf-8")
    observations_out = OBSERVATIONS_FILE.open("a", newline="", encoding="utf-8")
    landmarks_writer    = csv.writer(landmarks_out)
    observations_writer = csv.writer(observations_out)

    if landmarks_is_new:
        landmarks_writer.writerow(["id", "x", "y", "z"])
    if observations_is_new:
        observations_writer.writerow(["frame", "landmark_id", "pixel_x", "pixel_y", "visible"])

    ctx    = zmq.Context()
    socket = ctx.socket(zmq.SUB)
    socket.connect(ENDPOINT)
    socket.setsockopt(zmq.SUBSCRIBE, b"")
    socket.setsockopt(zmq.RCVTIMEO, RECV_TIMEOUT_MS)

    print(f"Connected to {ENDPOINT}")
    print(f"Saving frames to {FRAMES_DIR}")
    print(f"Saving landmarks to {LANDMARKS_FILE}")
    print(f"Saving observations to {OBSERVATIONS_FILE}")
    print(f"Resuming at frame_index={frame_index}, {len(known_landmark_ids)} known landmarks")
    print(f"Skipping the first {SKIP_FIRST_FRAMES} camera frames as warm-up")
    print("Press Ctrl+C to stop.\n")

    camera_frames_seen = 0
    last_camera_bytes  = None

    try:
        while True:
            try:
                parts = socket.recv_multipart()
            except zmq.Again:
                print("Waiting for simulator...", flush=True)
                continue

            if len(parts) < 2:
                continue

            try:
                envelope = json.loads(parts[0])
            except (json.JSONDecodeError, UnicodeDecodeError) as exc:
                print(f"Bad envelope: {exc}", file=sys.stderr)
                continue

            sensors = envelope.get("sensors", [])

            frame_name        = None
            objects_bytes     = None
            skipped_warm_up   = False
            skipped_duplicate = False

            for i, sensor in enumerate(sensors):
                payload_part = i + 1
                if payload_part >= len(parts):
                    continue

                topic = sensor.get("topic")

                if topic == "camera":
                    camera_frames_seen += 1
                    jpeg_bytes = parts[payload_part]

                    if camera_frames_seen <= SKIP_FIRST_FRAMES:
                        # Warm-up frame — don't decode/save it, and don't touch frame_index.
                        skipped_warm_up = True
                    elif jpeg_bytes == last_camera_bytes:
                        # Same JPEG bytes as last time — the camera encoder hasn't
                        # produced a new image yet. Recording this tick would pair a
                        # fresh cesium_objects projection with a stale picture.
                        skipped_duplicate = True
                    else:
                        sim_time   = sensor.get("timestamp", 0.0)
                        frame_name = f"frame_{frame_index:06d}_{sim_time:.4f}.jpg"
                        (FRAMES_DIR / frame_name).write_bytes(jpeg_bytes)

                    last_camera_bytes = jpeg_bytes

                elif topic == "cesium_objects":
                    objects_bytes = parts[payload_part]

            if skipped_warm_up or skipped_duplicate:
                # Discard any cesium_objects payload paired with this frame too — a
                # skipped frame must not contribute to any calculation.
                if skipped_warm_up:
                    print(f"[warm-up {camera_frames_seen}/{SKIP_FIRST_FRAMES}] frame skipped", flush=True)
                else:
                    print("[duplicate] frame skipped (camera hasn't produced a new image yet)", flush=True)
                continue

            if frame_name is None:
                continue

            new_landmarks   = 0
            observation_rows = 0

            if objects_bytes is not None:
                try:
                    payload = json.loads(objects_bytes.decode("utf-8"))
                except (json.JSONDecodeError, UnicodeDecodeError) as exc:
                    print(f"Bad cesium_objects payload: {exc}", file=sys.stderr)
                    payload = {"objects": []}

                for obj in payload.get("objects", []):
                    obj_id = obj.get("id", "")
                    if not obj_id:
                        continue

                    if obj_id not in known_landmark_ids:
                        known_landmark_ids.add(obj_id)
                        landmarks_writer.writerow([
                            obj_id,
                            obj.get("latitude", 0.0),
                            obj.get("longitude", 0.0),
                            obj.get("altitude", 0.0),
                        ])
                        new_landmarks += 1

                    observations_writer.writerow([
                        frame_index,
                        obj_id,
                        obj.get("pixel_x", -1),
                        obj.get("pixel_y", -1),
                        obj.get("visible", False),
                    ])
                    observation_rows += 1

                landmarks_out.flush()
                observations_out.flush()

            print(f"[{frame_index:6d}] {frame_name}"
                  f"  objects={observation_rows} (+{new_landmarks} new landmarks)")
            frame_index += 1

    except KeyboardInterrupt:
        print(f"\nStopped. {frame_index} frames, {len(known_landmark_ids)} landmarks total.")
    finally:
        landmarks_out.close()
        observations_out.close()
        socket.close()
        ctx.term()


if __name__ == "__main__":
    main()
