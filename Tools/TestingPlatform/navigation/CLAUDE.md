# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Running the system

Two separate terminals are required:

**Terminal 1 — classification worker (Process B):**
```
python classification_worker.py
```

**Terminal 2 — main loop (Process A):**
```
python main.py
```

Stop either with `Ctrl+C`, or press `Q` in the video window.

## Setup

Requires Python 3.10+ and `opencv-contrib-python` (not `opencv-python` — CSRT tracker is in contrib):
```
pip uninstall opencv-python -y
pip install -r requirements.txt
```

Verify tracker is available:
```
python -c "import cv2; print(cv2.legacy.TrackerCSRT_create())"
```

## Configuration

| Setting | File | Default |
|---|---|---|
| `MODEL_PATH` — path to YOLOv8 `.pt` weights | `main.py:20` | hardcoded local path |
| `FOCAL_LENGTH_PX` — camera focal length in pixels | `constants.py:20` | `320.0` (640×480, HFOV=90°) |
| `CAMERA_ZMQ_ADDRESS` — ZMQ SUB address of the simulator bus | `main.py:22` | `tcp://127.0.0.1:5555` |
| `SHOW_VIDEO` — display window | `main.py:23` | `True` |

Calibrate focal length: `focal_length = (pixel_width * known_distance_mm) / known_size_mm`

## Architecture

### Dual-Loop Asynchronous design

**Process A** (`main.py`) — real-time loop. Receives multipart ZMQ frames from the simulator, calls `CameraTickProcessor.tick(frame)` on each camera frame, renders overlay + side panel, returns `TargetState`. Must stay under 30 ms per frame.

**Process B** (`classification_worker.py`) — slow background process. Receives cropped target images over ZMQ PUSH/PULL, runs classification inference, returns `{drone_class, size_mm, kalman_q_variance}` to Process A. Uses `socket.poll(timeout=500)` instead of blocking `recv` so `Ctrl+C` works reliably.

### ZMQ message format (simulator → Process A)

Each ZMQ multipart message carries all sensors at once:
- `parts[0]` — envelope JSON: `{"sensors": [{"topic": "camera"}, {"topic": "lidar"}, ...]}`
- `parts[i+1]` — binary payload for `sensors[i]`

**Camera payload** (`topic: "camera"`): `[4-byte LE int: JSON length][JSON metadata][JPEG bytes]`

**Lidar payload** (`topic: "lidar"`): UTF-8 JSON `{"ActorName": distance_cm, ...}`. Keys are Unreal actor names, values are distances in Unreal units (centimeters). Process A takes `min(values) / 100.0` to get the closest-actor distance in metres and stores it in `TargetState.lidar_distance_m`.

### ZMQ ports

| Port | Direction | Purpose |
|---|---|---|
| `5555` | Simulator → Process A | Sensor bus (ZMQ SUB) |
| `5600` | Process A → Process B | Crop frames for classification (PUSH/PULL) |
| `5601` | Process B → Process A | Classification results (PUSH/PULL) |

### State machine (`state.py`)

`TargetState` is the single mutable target object shared across Process A:

| Field | Type | Meaning |
|---|---|---|
| `status` | `TargetStatus` | `SEARCHING → TRACKING → LOST` |
| `bbox` | `Optional[BBox]` | Current `(x, y, w, h)` in pixels, `None` when not tracking |
| `physical_size_mm` | `float` | Known physical size used for monocular formula |
| `kalman_q_variance` | `float` | Process noise intensity (high = agile, low = inertial) |
| `estimated_distance_m` | `float` | Kalman-filtered monocular estimate |
| `lidar_distance_m` | `Optional[float]` | Latest minimum lidar reading in metres, `None` until first lidar frame |

### `camera_tick_processor.py` — orchestration core

`CameraTickProcessor.tick(frame)` is the only per-frame entry point. Execution order:
1. Non-blocking IPC check — updates `physical_size_mm` and Kalman Q on new classifier result.
2. `SEARCHING`/`LOST`: runs `detector.detect(frame)`; on hit → init tracker, transition to `TRACKING`.
3. `TRACKING`: `tracker.update(frame)`; on confidence drop → `LOST`.
4. Every `REDETECT_INTERVAL_FRAMES` (=15) frames while tracking: re-runs detector to correct CSRT bbox drift on zoom.
5. `distance_estimator.calculate(bbox, size_mm, dt)` → updates `estimated_distance_m`.
6. Every `IPC_DISPATCHER_INTERVAL_FRAMES` (=5) frames: sends padded crop to classifier.

**Strict DI rule:** `camera_tick_processor.py` imports only the four `Protocol` interfaces (`IDetector`, `ITracker`, `IDistanceEstimator`, `IIPC`). All concrete wiring is in `main.py`.

### Distance estimator (`distance_estimator.py`)

Two-stage pipeline:
1. **Raw monocular formula**: `distance_m = (focal_px × size_mm) / (diag_px × 1000)`
2. **1D Kalman filter** — Constant Velocity (CV) model, state `[position, velocity]`.
   - **Adaptive R**: measurement noise grows as `(ref_diag / diag_px)²` — distant/small targets are trusted less than large/close ones.
   - **Dynamic Q**: `update_q(variance)` is called whenever the drone class changes; high Q for agile drones (FPV), low Q for inertial ones (heavy bomber).

### Overlay and side panel (`main.py`)

`draw_overlay(frame, state)` returns `np.hstack([annotated_frame, side_panel])` — the window is always `frame_width + 200` pixels wide.

Side panel columns (200 px, dark background):
- **ESTIMATED** — `estimated_distance_m` in status colour; `---` when `bbox is None`.
- **LIDAR** — `lidar_distance_m` in yellow; `---` until first lidar message.
- **ERROR** — `|estimated − lidar| / lidar × 100 %`; colour: green < 10 %, yellow < 25 %, red ≥ 25 %; `---` when either source is unavailable.

### Module responsibilities

| Module | Role |
|---|---|
| `detector.py` | `DroneDetector` — YOLOv8 inference, returns best-confidence `BBox` or `None` |
| `tracker.py` | `DroneTracker` — CSRT wrapper; `init(frame, bbox)` + `update(frame) → (bool, BBox)` |
| `distance_estimator.py` | CV-model Kalman filter + monocular formula with adaptive R |
| `ipc_communicator.py` | `IPCCommunicator` facade: `FramePusher` (PUSH, SNDHWM=1, non-blocking) + `ResultPuller` (PULL, non-blocking) |
| `constants.py` | `KNOWN_DRONES_DB`, focal length, frame-interval constants |
| `state.py` | `TargetState` dataclass + `TargetStatus` enum |

### Drone database (`constants.py`)

`KNOWN_DRONES_DB` maps class name → `{size_mm, kalman_q_variance}`. To add a drone class: add an entry here and train the classifier.

## Current limitations / stubs

- `classification_worker._classify()` is a **stub** — always returns `CESSNA_172` with a 0.5 s simulated delay. Replace the body with real inference when the model is ready.
- `architecture_context.txt` specifies per-aspect-angle entries (`MAVIC_3_FRONT`, `MAVIC_3_SIDE`, etc.); current `constants.py` uses single-entry classes. The `physical_size_mm` field will need to split into per-aspect keys once the classifier predicts aspect angle.
- Lidar-to-target association uses `min(lidar_scan.values())` — closest actor across all directions. This works for single-target scenarios but would need spatial projection (camera ray ↔ lidar point) for multi-actor environments.
