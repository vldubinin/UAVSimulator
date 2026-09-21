"""
Records the onboard camera stream of UAVSimulator into an MP4 file — clean video:
no text, no bboxes, no markers; the frames are written exactly as decoded from the bus.

Subscribes (ZMQ SUB) to the sensor bus published by USensorBusComponent and takes the JPEG
frames of the camera topic from every multipart message (docs: Docs/04-SensorBus.md):
    Part 0:   JSON envelope {"timestamp": T, "sensors": [{"topic": "...", "timestamp": T1}, ...]}
    Part 1..N raw payload of each sensor, in the same order as "sensors".

Run without arguments (settings are the constants below):
    py record_camera_video.py

Output: <this script's folder>/camera_recordings/camera_YYYYmmdd_HHMMSS.mp4
Stop with Ctrl+C or 'q' / Esc in the preview window (the preview shows the same clean frames).

SPEED. The bus publishes at BusRate, while the camera produces a NEW frame only at MaxEncodeFPS, so the
same frame often arrives several times; and the arrival rate is not constant. Writing every received
frame at a guessed FPS therefore plays too fast/slow/jerky. Instead:
  * a frame is taken only when its camera timestamp is new (duplicates are dropped);
  * the video has a CONSTANT frame rate (OUTPUT_FPS) and the frames are placed on that grid by their
    camera (world) timestamps — a frame is repeated if the camera was slower than OUTPUT_FPS, so the
    video plays at exactly the speed the simulation ran;
  * long stalls (simulation paused, > MAX_GAP_S) are collapsed instead of frozen into the video.

QUALITY.
  * Encoder: ffmpeg + libx264 (CRF, visually lossless-ish at 16) if ffmpeg is found — in PATH or via
    `pip install imageio-ffmpeg` (bundled binary). Without it the script falls back to OpenCV's
    'mp4v', which is noticeably worse (a warning is logged).
  * The bus carries JPEG (UAVCameraComponent::JpegQuality, default 80, MaxEncodeFPS 30). The video cannot
    be better than that source: raise JpegQuality on the camera in the simulator if you need more.
  * Frames are never resized (unless the camera resolution changes mid-recording).

Requires: pip install pyzmq opencv-python numpy imageio-ffmpeg
"""

import json
import logging
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from datetime import datetime
from typing import Optional

import cv2
import numpy as np
import zmq

# ─── Settings ────────────────────────────────────────────────────────────────
ENDPOINT        = "tcp://127.0.0.1:5555"    # ZMQ PUB of USensorBusComponent
CAMERA_TOPICS   = ["camera_tp", "camera"]   # first one present on the bus wins
OUT_DIR         = os.path.join(os.path.dirname(os.path.abspath(__file__)), "camera_recordings")

OUTPUT_FPS      = 30.0    # constant frame rate of the video; frames are placed by camera timestamps
MAX_GAP_S       = 2.0     # a pause longer than this in the camera timestamps is collapsed
CRF             = 16      # libx264 quality: lower = better/larger (14-18 is visually lossless-ish)
PRESET          = "medium"  # libx264 speed/size trade-off: ultrafast ... slow
FALLBACK_FOURCC = "mp4v"  # used only when ffmpeg is not available

SHOW_PREVIEW    = True
QUEUE_SIZE      = 600     # received frames waiting for processing (~20 s at 30 fps)
RECV_TIMEOUT_MS = 100
STATS_INTERVAL_S = 3.0

log = logging.getLogger("camera_rec")


# ─── Video sinks ─────────────────────────────────────────────────────────────

def find_ffmpeg() -> Optional[str]:
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return None


class FfmpegSink:
    """Pipes raw BGR frames to `ffmpeg -c:v libx264`."""

    def __init__(self, exe: str, path: str, size: tuple[int, int], fps: float) -> None:
        cmd = [exe, "-y", "-loglevel", "error",
               "-f", "rawvideo", "-pix_fmt", "bgr24", "-s", f"{size[0]}x{size[1]}", "-r", f"{fps}", "-i", "-",
               "-c:v", "libx264", "-preset", PRESET, "-crf", str(CRF),
               "-pix_fmt", "yuv420p", "-movflags", "+faststart", path]
        self._proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        self.name = f"ffmpeg/libx264 crf={CRF} preset={PRESET}"

    def write(self, frame: np.ndarray) -> None:
        self._proc.stdin.write(frame.tobytes())

    def close(self) -> None:
        try:
            self._proc.stdin.close()
        except OSError:
            pass
        self._proc.wait(timeout=60)
        if self._proc.returncode:
            log.error("ffmpeg exited with code %s", self._proc.returncode)


class CvSink:
    """Fallback: OpenCV VideoWriter (mp4v). Lower quality than libx264."""

    def __init__(self, path: str, size: tuple[int, int], fps: float) -> None:
        self._writer = cv2.VideoWriter(path, cv2.VideoWriter_fourcc(*FALLBACK_FOURCC), fps, size)
        if not self._writer.isOpened():
            raise RuntimeError(f"cannot open VideoWriter (codec '{FALLBACK_FOURCC}', size {size})")
        self.name = f"OpenCV {FALLBACK_FOURCC} (fallback)"

    def write(self, frame: np.ndarray) -> None:
        self._writer.write(frame)

    def close(self) -> None:
        self._writer.release()


def open_sink(path: str, size: tuple[int, int], fps: float):
    exe = find_ffmpeg()
    if exe:
        try:
            return FfmpegSink(exe, path, size, fps)
        except OSError as e:
            log.warning("cannot start ffmpeg (%s): falling back to OpenCV", e)
    else:
        log.warning("ffmpeg not found -> falling back to OpenCV 'mp4v' (worse quality). "
                    "For a good video: pip install imageio-ffmpeg")
    return CvSink(path, size, fps)


# ─── Frame timeline: constant FPS on the camera-timestamp grid ───────────────

class Recorder:
    """Runs in the worker thread: decode -> drop duplicates -> place on the constant-FPS grid -> encode."""

    def __init__(self, output: str) -> None:
        self.output = output
        self.sink = None
        self.size: Optional[tuple[int, int]] = None
        self.prev: Optional[np.ndarray] = None   # frame that is valid from prev_ts until the next frame arrives
        self.t0: Optional[float] = None          # camera timestamp mapped to video time 0
        self.last_ts: Optional[float] = None
        self.prev_written = False                # has `prev` already been written to at least one slot
        self.slot = 0                            # next video frame index to write
        self.received = self.unique = self.written = self.repeated = 0

    def _write(self, frame: np.ndarray) -> None:
        self.sink.write(frame)
        self.written += 1

    def push(self, frame: np.ndarray, ts: float) -> None:
        self.received += 1
        if self.last_ts is not None and ts == self.last_ts:
            return                                   # same camera frame delivered again by the bus
        if self.size is None:
            self.size = (frame.shape[1], frame.shape[0])
            self.sink = open_sink(self.output, self.size, OUTPUT_FPS)
            log.info("recording started: %dx%d @ %.2f fps constant, encoder: %s",
                     self.size[0], self.size[1], OUTPUT_FPS, self.sink.name)
        elif (frame.shape[1], frame.shape[0]) != self.size:
            frame = cv2.resize(frame, self.size)
        self.unique += 1

        if self.t0 is None:
            self.t0 = ts
        elif ts < self.last_ts:
            log.warning("camera timestamp went backwards (simulation restarted?): re-syncing the timeline")
            self.t0 = ts - self.slot / OUTPUT_FPS
        elif ts - self.last_ts > MAX_GAP_S:
            log.warning("camera stalled for %.1f s (simulation paused?): gap collapsed", ts - self.last_ts)
            self.t0 += (ts - self.last_ts) - 1.0 / OUTPUT_FPS

        rel = ts - self.t0
        # The previous frame stays on screen until the new one is nearer to a slot than it is.
        while self.prev is not None and (self.slot + 0.5) / OUTPUT_FPS <= rel:
            if self.prev_written:
                self.repeated += 1                   # this frame already filled an earlier slot
            self._write(self.prev)
            self.prev_written = True
            self.slot += 1
        self.prev = frame
        self.prev_written = False
        self.last_ts = ts

    def finish(self) -> None:
        if self.sink is None:
            return
        if self.prev is not None and not self.prev_written:
            self._write(self.prev)                   # the last frame, so it is not lost
        self.sink.close()


# ─── Main ────────────────────────────────────────────────────────────────────

def worker(frames: "queue.Queue", recorder: Recorder, stop: threading.Event) -> None:
    while not (stop.is_set() and frames.empty()):
        try:
            jpeg, ts = frames.get(timeout=0.1)
        except queue.Empty:
            if SHOW_PREVIEW and cv2.waitKey(1) in (ord("q"), 27):
                stop.set()
            continue

        frame = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
        if frame is None:
            log.error("failed to decode camera JPEG (%d bytes)", len(jpeg))
            continue
        try:
            recorder.push(frame, ts)
        except (OSError, RuntimeError) as e:
            log.error("encoder error: %s", e)
            stop.set()
            break

        if SHOW_PREVIEW:
            cv2.imshow("UAV camera (recording)", frame)
            if cv2.waitKey(1) in (ord("q"), 27):
                stop.set()


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-7s %(message)s", datefmt="%H:%M:%S")

    os.makedirs(OUT_DIR, exist_ok=True)
    output = os.path.join(OUT_DIR, "camera_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".mp4")

    ctx = zmq.Context()
    socket = ctx.socket(zmq.SUB)
    socket.setsockopt(zmq.RCVHWM, 100)
    socket.setsockopt(zmq.SUBSCRIBE, b"")
    socket.setsockopt(zmq.RCVTIMEO, RECV_TIMEOUT_MS)
    socket.connect(ENDPOINT)

    log.info("SUB socket connecting to %s (ZMQ connects lazily: no error if the simulator is not up yet)", ENDPOINT)
    log.info("camera topic(s): %s", CAMERA_TOPICS)
    log.info("output: %s", output)
    log.info("press Ctrl+C%s to stop", " or 'q' / Esc in the preview window" if SHOW_PREVIEW else "")

    frames: "queue.Queue" = queue.Queue(maxsize=QUEUE_SIZE)
    stop = threading.Event()
    recorder = Recorder(output)
    thread = threading.Thread(target=worker, args=(frames, recorder, stop), name="encoder")
    thread.start()

    messages = dropped = 0
    topics_seen: dict[str, int] = {}
    last_report = time.monotonic()
    try:
        while not stop.is_set():
            now = time.monotonic()
            if now - last_report >= STATS_INTERVAL_S:
                last_report = now
                if messages == 0:
                    log.warning("no bus messages received yet: check that the simulation is running (Start pressed), "
                                "the endpoint is correct and the airplane has a USensorBusComponent with enabled sensors")
                else:
                    log.info("messages=%d received=%d unique=%d written=%d (repeated=%d) queue_dropped=%d topics=%s",
                             messages, recorder.received, recorder.unique, recorder.written, recorder.repeated,
                             dropped, topics_seen)
                    if not any(t in topics_seen for t in CAMERA_TOPICS):
                        log.warning("camera topic %s NOT seen on the bus. Seen topics: %s -> edit CAMERA_TOPICS "
                                    "(or enable the Camera Frame sensor)", CAMERA_TOPICS, sorted(topics_seen))

            try:
                parts = socket.recv_multipart()
            except zmq.Again:
                continue

            try:
                envelope = json.loads(parts[0].decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError) as e:
                log.error("bad bus envelope: %s", e)
                continue

            messages += 1
            sensors = envelope.get("sensors", [])
            for s in sensors:
                topics_seen[s.get("topic")] = topics_seen.get(s.get("topic"), 0) + 1

            jpeg, ts, best = None, 0.0, len(CAMERA_TOPICS)
            for i, sensor in enumerate(sensors, start=1):
                if i >= len(parts):
                    break
                topic = sensor.get("topic")
                if topic in CAMERA_TOPICS and CAMERA_TOPICS.index(topic) < best:
                    best = CAMERA_TOPICS.index(topic)
                    jpeg = bytes(parts[i])
                    ts = float(sensor.get("timestamp", 0.0))
            if jpeg is None:
                continue
            try:
                frames.put_nowait((jpeg, ts))
            except queue.Full:
                dropped += 1
                if dropped == 1 or dropped % 100 == 0:
                    log.warning("encoder is too slow, %d frame(s) dropped (raise PRESET speed or lower CRF cost)",
                                dropped)

    except KeyboardInterrupt:
        log.info("interrupted by user")
    finally:
        stop.set()
        thread.join()
        recorder.finish()
        if recorder.sink is not None:
            duration = recorder.written / OUTPUT_FPS
            log.info("done: %d frames written (%d repeated to keep the speed), video length %.1f s -> %s",
                     recorder.written, recorder.repeated, duration, output)
        else:
            log.warning("no camera frames were received: no video file created")
        socket.close()
        ctx.term()
        if SHOW_PREVIEW:
            cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    sys.exit(main())
