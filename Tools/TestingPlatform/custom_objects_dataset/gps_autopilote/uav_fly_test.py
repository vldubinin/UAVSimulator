"""
UavFlyTest — головний файл системи: приймає з ZMQ (SensorBusComponent,
tcp://*:5555 — PUB, мультипарт-повідомлення: part 0 = JSON-конверт
{"sensors": [{"topic": ...}, ...]}, part 1..N = сирі payload'и сенсорів у тому
самому порядку) кадри камери й дані сенсорів симулятора БПЛА (UAVSimulator),
рахує власну (PnP) позицію дрона з відео, і на кожному кадрі:
  - викликає Autopilote.compute_command(...) і надсилає повернуті команди
    керування назад по ZMQ PUSH -> tcp://127.0.0.1:5556 (SET_ATTITUDE_TARGET,
    приймає UAttitudeControlComponent, режим симуляції — "Playback and Auto Track");
  - викликає Laboratory.update(...) для відображення карти (звірка розрахованої
    позиції з фактичною).

Використовує сенсори:
  - "camera"         — JPEG-кадр (CameraFrameComponent, 640x480, HFOV=90°).
  - "cesium_objects" — {"objects": [{"id","latitude","longitude","altitude",
                        "pixel_x","pixel_y","visible"}, ...]} — видимі орієнтири
                        з РЕАЛЬНИМИ глобальними координатами.
  - "custom_objects" — той самий формат (якщо замість/разом із cesium_objects).
  - "drone_geo_position" — {"latitude","longitude","altitude_m"} — РЕАЛЬНА
                        (симуляційна) позиція дрона; саме вона передається в
                        Autopilote.compute_command як позиція дрона (PnP-розрахована
                        позиція з зображення в керування НЕ йде — вона рахується
                        лише для відображення/звірки на карті в Laboratory).

Підхід до позиціювання: детекції YOLO зіставляються з видимими орієнтирами по
близькості центру bbox до pixel_x/pixel_y (match_detection_to_landmark) —
координати об'єктів беруться напряму з орієнтира; ті самі зіставлення (2D-піксель
<-> відома 3D geo-точка) йдуть у cv2.solvePnPRansac (estimate_drone_position) —
звідси власна позиція дрона (позиція камери).

YOLO не блокує відеопотік: інференс запускається у фоновому потоці
(DetectionPipeline, ThreadPoolExecutor(max_workers=1)) — новий кадр передається
в YOLO лише тоді, коли попередній уже опрацьовано; кадри, що надходять, поки
YOLO зайнятий, пропускаються.

Стабілізація PnP-позиції: DetectionPipeline зіставляє детекції з орієнтирами
проти знімку орієнтирів, зафіксованого в момент відправки того самого кадру в
YOLO (а не проти "поточних"); PoseSmoother згладжує сирий вихід
estimate_drone_position (EMA) і відкидає фізично неможливі "стрибки"; курс
(_HeadingEstimator) оцінюється не між сусідніми кадрами відео, а раз на
HEADING_UPDATE_INTERVAL_S секунд. Швидкість дрона ніде не рахується й не
використовується.

Запуск: `python uav_fly_test.py` під час активної симуляції з увімкненим
SensorBusComponent (і хоча б одним із cesium_objects/custom_objects сенсорів),
режим симуляції "Playback and Auto Track". 'q' у вікні відео завершує роботу.
"""

from __future__ import annotations

import csv
import json
import math
import time
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import cv2
import numpy as np
import zmq
from ultralytics import YOLO

from autopilote import Autopilote
from common import ControlCommand, Position, latlon_to_local_m, local_m_to_latlon
from laboratory import Laboratory

# ── Конфігурація ────────────────────────────────────────────────────────────

ZMQ_ENDPOINT: str = "tcp://127.0.0.1:5555"  # SUB підключається до PUB симулятора
CMD_ENDPOINT: str = "tcp://127.0.0.1:5556"  # ZMQ PUSH -> PULL у UAttitudeControlComponent

WEIGHTS_PATH = Path(__file__).resolve().parent / "full-set-best.pt"

WAYPOINT_RADIUS_M: float = 100.0  # точка вважається досягнутою в межах цього радіуса
CRUISE_THRUST: float = 0.70       # нейтральна тяга для команд поза активним керуванням Autopilote

# Внутрішні параметри камери — CameraFrameComponent завжди віддає 640x480,
# HFOV=90° -> fx=fy=320px, головна точка в центрі кадру, дисторсія відсутня.
IMG_W, IMG_H = 640, 480
FX = FY = 320.0
CX, CY = IMG_W / 2.0, IMG_H / 2.0
CAMERA_MATRIX = np.array([[FX, 0, CX], [0, FY, CY], [0, 0, 1]], dtype=np.float64)
DIST_COEFFS = np.zeros(4, dtype=np.float64)

MIN_PNP_POINTS = 6       # запас понад мінімальні 4 точки EPnP — щоб RANSAC міг відкинути 1-2 хибні
PNP_REPROJECTION_ERROR_PX = 8.0  # поріг inlier'а для cv2.solvePnPRansac, пікселі
MAX_POSE_SPEED_MPS = 60.0  # фізично неможливий "стрибок" сирого PnP-фіксу відкидається

CONF_THRESHOLD: float = 0.25

# Поріг зіставлення детекції з орієнтиром: частка діагоналі кадру.
MATCH_MAX_DIST_FRAC: float = 0.15
MATCH_MAX_DIST_PX: float = MATCH_MAX_DIST_FRAC * math.hypot(IMG_W, IMG_H)

# Ключові точки маршруту: (latitude, longitude, altitude_m). Порожній список =
# дрон не керується, скрипт лише спостерігає.
WAYPOINTS: list[Position] = [
    (50.0463231, 36.2817064, 100.0),
    (50.04395295027201, 36.288967727181, 90.0),
    (50.0455224, 36.2907573, 90.0),
    (50.0467817, 36.2897081, 90.0),
    (50.0492247, 36.2901183, 100.0),
    (50.05061075454689, 36.29631391551742, 70.0),
]

MIN_NAV_DISPLACEMENT_M: float = 2.0  # мінімальне зміщення між PnP-фіксами для оновлення курсу
HEADING_UPDATE_INTERVAL_S: float = 0.5  # мінімальний час між семплами для оцінки курсу — на
                                         # короткій базі (сусідні кадри відео) шум згладженої
                                         # PnP-позиції порівнянний із самим переміщенням.

LOG_DIR = Path(__file__).resolve().parent / "logs"  # CSV-лог команд керування


# ── Неблокуючий YOLO: кадри між інференсами пропускаються ───────────────────

Detection = tuple[int, float, tuple[float, float, float, float]]  # (cls_id, conf, xyxy)


class DetectionPipeline:
    """
    YOLO працює у фоновому потоці (max_workers=1): новий кадр передається в
    YOLO лише тоді, коли попередній інференс уже завершився (self._pending is
    None). Усі кадри, що надходять, поки YOLO зайнятий, просто пропускаються —
    tick() одразу повертає останній отриманий результат YOLO і ніколи не
    чекає на інференс, тож відеопотік не блокується.

    Зіставлення детекцій з орієнтирами (build_matches) виконується тут-таки,
    ОДРАЗУ як інференс завершиться, і саме проти self._dispatch_landmarks —
    знімку списку орієнтирів, зробленого В ТОЙ САМИЙ МОМЕНТ, коли відповідний
    кадр було відправлено в YOLO (а не проти "поточних" — інакше 2D-піксель
    відв'язується від 3D-точки, з якою його зіставляли).
    """

    def __init__(self, model: YOLO) -> None:
        self._model = model
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="yolo")
        self._pending: Optional[Future] = None
        self._dispatch_landmarks: list["Landmark"] = []
        self._last_matches: list["Match"] = []
        self.last_latency_ms: Optional[float] = None

    def close(self) -> None:
        self._executor.shutdown(wait=False, cancel_futures=True)

    def tick(self, frame: np.ndarray, landmarks: list["Landmark"]) -> list["Match"]:
        if self._pending is not None and self._pending.done():
            result, latency_ms = self._pending.result()
            self._pending = None
            self.last_latency_ms = latency_ms

            detections: list[Detection] = []
            boxes = result.boxes
            for cls_id, conf, xyxy in zip(boxes.cls.tolist(), boxes.conf.tolist(), boxes.xyxy.tolist()):
                if conf < CONF_THRESHOLD:
                    continue
                detections.append((int(cls_id), conf, tuple(xyxy)))
            self._last_matches = build_matches(detections, self._dispatch_landmarks)

        if self._pending is None:
            self._dispatch_landmarks = landmarks
            self._pending = self._executor.submit(self._run_yolo, frame.copy())

        return self._last_matches

    def _run_yolo(self, frame: np.ndarray) -> tuple[object, float]:
        t0 = time.perf_counter()
        result = self._model.predict(frame, verbose=False)[0]
        latency_ms = (time.perf_counter() - t0) * 1000.0
        return result, latency_ms


# ── Відомі орієнтири (cesium_objects / custom_objects) ──────────────────────

class Landmark:
    __slots__ = ("id", "latitude", "longitude", "altitude", "pixel_x", "pixel_y")

    def __init__(self, id_: str, latitude: float, longitude: float, altitude: float,
                 pixel_x: float, pixel_y: float) -> None:
        self.id = id_
        self.latitude = latitude
        self.longitude = longitude
        self.altitude = altitude
        self.pixel_x = pixel_x
        self.pixel_y = pixel_y


def parse_landmarks(payload: bytes) -> list[Landmark]:
    """Розбирає payload cesium_objects/custom_objects у список видимих орієнтирів."""
    try:
        data = json.loads(payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return []

    landmarks: list[Landmark] = []
    for obj in data.get("objects", []):
        if not obj.get("visible", True):
            continue
        px, py = obj.get("pixel_x", -1), obj.get("pixel_y", -1)
        if px < 0 or py < 0:
            continue
        landmarks.append(Landmark(
            obj.get("id", ""), obj.get("latitude", 0.0), obj.get("longitude", 0.0),
            obj.get("altitude", 0.0), px, py,
        ))
    return landmarks


def match_detection_to_landmark(
    cx_px: float, cy_px: float, landmarks: list[Landmark], used: set[str]
) -> Optional[Landmark]:
    """Найближчий (у пікселях) ще не використаний орієнтир у межах MATCH_MAX_DIST_PX."""
    best: Optional[Landmark] = None
    best_dist = math.inf
    for lm in landmarks:
        if lm.id in used:
            continue
        d = math.hypot(cx_px - lm.pixel_x, cy_px - lm.pixel_y)
        if d < best_dist:
            best_dist, best = d, lm
    if best is not None and best_dist <= MATCH_MAX_DIST_PX:
        return best
    return None


# Один запис зіставлення: (cls_id, conf, xyxy, cx_px, cy_px, landmark|None).
Match = tuple[int, float, tuple[float, float, float, float], float, float, Optional[Landmark]]


def build_matches(detections: list[Detection], landmarks: list[Landmark]) -> list[Match]:
    used: set[str] = set()
    out: list[Match] = []
    for cls_id, conf, xyxy in detections:
        x1, y1, x2, y2 = xyxy
        cx_px, cy_px = (x1 + x2) / 2.0, (y1 + y2) / 2.0
        match = match_detection_to_landmark(cx_px, cy_px, landmarks, used)
        if match is not None:
            used.add(match.id)
        out.append((cls_id, conf, xyxy, cx_px, cy_px, match))
    return out


# ── PnP-оцінка власної позиції дрона ─────────────────────────────────────────

def estimate_drone_position(
    matches: list[Match], ref_lat: float, ref_lon: float
) -> Optional[Position]:
    """
    cv2.solvePnPRansac замість "голого" solvePnP: один хибний 2D-3D збіг здатен
    зіпсувати ввесь розв'язок EPnP. RANSAC підбирає найбільшу узгоджену
    підмножину точок (inliers, поріг PNP_REPROJECTION_ERROR_PX) і рахує позу
    лише по ній, ігноруючи викиди.
    """
    pnp_pairs = [(cx, cy, lm) for _, _, _, cx, cy, lm in matches if lm is not None]
    if len(pnp_pairs) < MIN_PNP_POINTS:
        return None

    image_pts = np.array([[cx, cy] for cx, cy, _ in pnp_pairs], dtype=np.float64)
    world_pts = np.array([
        (*latlon_to_local_m(lm.latitude, lm.longitude, ref_lat, ref_lon), lm.altitude)
        for _, _, lm in pnp_pairs
    ], dtype=np.float64)

    ok, rvec, tvec, inliers = cv2.solvePnPRansac(
        world_pts, image_pts, CAMERA_MATRIX, DIST_COEFFS,
        reprojectionError=PNP_REPROJECTION_ERROR_PX, confidence=0.99, iterationsCount=200,
        flags=cv2.SOLVEPNP_EPNP,
    )
    if not ok or inliers is None or len(inliers) < 4:
        return None

    R, _ = cv2.Rodrigues(rvec)
    cam_pos = (-R.T @ tvec).flatten()  # позиція камери у світових (схід, північ, вгору) координатах
    lat, lon = local_m_to_latlon(cam_pos[0], cam_pos[1], ref_lat, ref_lon)
    alt = cam_pos[2]
    if not all(math.isfinite(v) for v in (lat, lon, alt)):
        return None
    return lat, lon, alt


@dataclass
class PoseSmoother:
    """
    Експоненційне згладжування сирої PnP-позиції (lat/lon/alt), з попереднім
    відсіюванням фізично неможливих "стрибків" (implied_speed > MAX_POSE_SPEED_MPS):
    навіть після RANSAC поодинокий кадр може дати позицію, що "телепортує"
    дрона на сотні метрів. Такий сирий фікс просто ігнорується — повертається
    попередня згладжена оцінка.
    """
    alpha: float = 0.25  # менше -> сильніше згладжування, повільніша реакція на реальний рух
    lat: Optional[float] = None
    lon: Optional[float] = None
    alt: Optional[float] = None
    _last_time: Optional[float] = None

    def update(self, raw: Position, now: float) -> Position:
        raw_lat, raw_lon, raw_alt = raw
        if self.lat is None:
            self.lat, self.lon, self.alt, self._last_time = raw_lat, raw_lon, raw_alt, now
            return self.lat, self.lon, self.alt

        dt = now - self._last_time if self._last_time is not None else 0.0
        if dt > 1e-3:
            east, north = latlon_to_local_m(raw_lat, raw_lon, self.lat, self.lon)
            implied_speed = math.hypot(east, north, raw_alt - self.alt) / dt
            if implied_speed > MAX_POSE_SPEED_MPS:
                return self.lat, self.lon, self.alt  # відкидаємо викид, лишаємо попередню оцінку

        self.lat = self.alpha * raw_lat + (1.0 - self.alpha) * self.lat
        self.lon = self.alpha * raw_lon + (1.0 - self.alpha) * self.lon
        self.alt = self.alpha * raw_alt + (1.0 - self.alpha) * self.alt
        self._last_time = now
        return self.lat, self.lon, self.alt


# ── Оцінка курсу дрона з послідовних розрахованих позицій ───────────────────

class _HeadingEstimator:
    """
    Рахує "розрахований поточний курс дрона" з послідовних згладжених
    PnP-позицій — троттлено на HEADING_UPDATE_INTERVAL_S секунд і мінімальне
    зміщення MIN_NAV_DISPLACEMENT_M, щоб шум одиночного PnP-фіксу не домінував
    над реальним переміщенням дрона за один тік. Швидкість дрона свідомо не
    рахується й нікуди не передається.
    """

    def __init__(self) -> None:
        self._ref_east: Optional[float] = None
        self._ref_north: Optional[float] = None
        self._ref_time: Optional[float] = None
        self.heading_deg: Optional[float] = None

    def update(self, position: Position, ref_lat: float, ref_lon: float, now: float) -> Optional[float]:
        east, north = latlon_to_local_m(position[0], position[1], ref_lat, ref_lon)
        if self._ref_time is None:
            self._ref_east, self._ref_north, self._ref_time = east, north, now
            return self.heading_deg

        elapsed = now - self._ref_time
        if elapsed < HEADING_UPDATE_INTERVAL_S:
            return self.heading_deg

        dx, dy = east - self._ref_east, north - self._ref_north
        dist = math.hypot(dx, dy)
        if dist >= MIN_NAV_DISPLACEMENT_M:
            self.heading_deg = math.degrees(math.atan2(dx, dy)) % 360.0
        self._ref_east, self._ref_north, self._ref_time = east, north, now
        return self.heading_deg


# ── ZMQ приймання кадрів + сенсорів ─────────────────────────────────────────

EnvelopeResult = tuple[Optional[np.ndarray], Optional[list[Landmark]], Optional[Position]]


def process_envelope(parts: list[bytes]) -> EnvelopeResult:
    """Розбирає мультипарт-повідомлення шини. Повертає (кадр камери, орієнтири, реальна
    geo-позиція дрона) — кожне або None, якщо відповідний топік не прийшов у цьому повідомленні."""
    try:
        envelope = json.loads(parts[0].decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None, None, None

    frame: Optional[np.ndarray] = None
    landmarks: Optional[list[Landmark]] = None
    real_pose: Optional[Position] = None

    for i, sensor_info in enumerate(envelope.get("sensors", [])):
        if i + 1 >= len(parts):
            break
        topic = sensor_info.get("topic")
        payload = parts[i + 1]

        if topic == "camera":
            frame = cv2.imdecode(np.frombuffer(payload, np.uint8), cv2.IMREAD_COLOR)
        elif topic in ("cesium_objects", "custom_objects"):
            landmarks = parse_landmarks(payload)
        elif topic == "drone_geo_position":
            try:
                data = json.loads(payload.decode("utf-8"))
                real_pose = (data["latitude"], data["longitude"], data["altitude_m"])
            except (json.JSONDecodeError, UnicodeDecodeError, KeyError):
                pass

    return frame, landmarks, real_pose


# ── Візуалізація одного кадру ───────────────────────────────────────────────

def draw_and_report(
    frame: np.ndarray,
    matches: list[Match],
    landmarks_visible: int,
    drone_pose: Optional[Position],
    nav_status: Optional[str],
    class_names: dict[int, str],
    video_fps: float,
    yolo_latency_ms: Optional[float],
) -> np.ndarray:
    vis = frame.copy()
    n_matched = 0

    for cls_id, conf, xyxy, cx_px, cy_px, match in matches:
        x1, y1, x2, y2 = xyxy
        name = class_names.get(int(cls_id), str(int(cls_id)))

        cv2.rectangle(vis, (int(x1), int(y1)), (int(x2), int(y2)), (0, 255, 0), 2)
        cv2.drawMarker(vis, (int(cx_px), int(cy_px)), (0, 0, 255), cv2.MARKER_CROSS, 8, 1)

        label = f"{name} {conf:.2f}"
        cv2.putText(vis, label, (int(x1), max(0, int(y1) - 6)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1, cv2.LINE_AA)

        if match is not None:
            n_matched += 1
            geo_text = f"{match.latitude:.6f}, {match.longitude:.6f}"
            cv2.putText(vis, geo_text, (int(x1), int(y2) + 16),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 220, 255), 1, cv2.LINE_AA)
            print(f"{name:<20s} conf={conf:.2f}  px=({cx_px:6.1f},{cy_px:6.1f})  "
                  f"id={match.id}  lat={match.latitude:.6f}  lon={match.longitude:.6f}  "
                  f"alt={match.altitude:.1f}")
        else:
            cv2.putText(vis, "geo: n/a (no landmark match)", (int(x1), int(y2) + 16),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 255), 1, cv2.LINE_AA)
            print(f"{name:<20s} conf={conf:.2f}  px=({cx_px:6.1f},{cy_px:6.1f})  "
                  f"geo: н/д (немає відповідного орієнтира)")

    if drone_pose is not None:
        lat, lon, alt = drone_pose
        drone_text = f"DRONE: lat={lat:.6f} lon={lon:.6f} alt={alt:.1f} m  (PnP, {n_matched} pts)"
        drone_color = (0, 255, 0)
        print(f"[DRONE] lat={lat:.6f}  lon={lon:.6f}  alt={alt:.1f}  (PnP, {n_matched} орієнтирів)")
    else:
        drone_text = f"DRONE: n/a (need >= {MIN_PNP_POINTS} matched landmarks, have {n_matched})"
        drone_color = (0, 0, 255)
    cv2.putText(vis, drone_text, (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.55, drone_color, 2, cv2.LINE_AA)

    if nav_status is not None:
        cv2.putText(vis, f"NAV: {nav_status}", (8, 44), cv2.FONT_HERSHEY_SIMPLEX, 0.45,
                    (255, 200, 0), 1, cv2.LINE_AA)

    status = f"landmarks visible: {landmarks_visible}"
    cv2.putText(vis, status, (8, IMG_H - 26), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)

    if yolo_latency_ms is not None:
        speed = f"video: {video_fps:5.1f} fps   yolo: {yolo_latency_ms:6.1f} ms ({1000.0 / yolo_latency_ms:4.1f} fps)"
    else:
        speed = f"video: {video_fps:5.1f} fps   yolo: waiting for first result..."
    cv2.putText(vis, speed, (8, IMG_H - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)

    return vis


def build_command(roll_deg: float, pitch_deg: float, yaw_rate_deg_s: float, thrust: float) -> dict:
    """SET_ATTITUDE_TARGET — кути в РАДІАНАХ (UAttitudeControlComponent порівнює їх
    напряму з поточною орієнтацією без конверсії)."""
    return {
        "command_type": "SET_ATTITUDE_TARGET",
        "roll": math.radians(roll_deg),
        "pitch": math.radians(pitch_deg),
        "yaw_rate": math.radians(yaw_rate_deg_s),
        "thrust": float(thrust),
    }


# ── Головний клас системи ────────────────────────────────────────────────────

class UavFlyTest:
    """
    Стартує систему: підключається до ZMQ (SUB — сенсори симулятора,
    PUSH — команди керування), рахує власну позицію дрона з відео, і на
    кожному кадрі об'єднує Autopilote (команди керування) та Laboratory
    (відображення карти).
    """

    def __init__(self) -> None:
        print(f"Завантаження моделі: {WEIGHTS_PATH}")
        model = YOLO(str(WEIGHTS_PATH))
        self._class_names: dict[int, str] = model.names
        self._pipeline = DetectionPipeline(model)

        self._zmq_context = zmq.Context()
        self._sensor_socket = self._zmq_context.socket(zmq.SUB)
        self._sensor_socket.setsockopt(zmq.RCVHWM, 2)
        self._sensor_socket.connect(ZMQ_ENDPOINT)
        self._sensor_socket.setsockopt_string(zmq.SUBSCRIBE, "")

        self._cmd_socket = self._zmq_context.socket(zmq.PUSH)
        self._cmd_socket.setsockopt(zmq.SNDHWM, 1)
        self._cmd_socket.setsockopt(zmq.LINGER, 0)
        self._cmd_socket.connect(CMD_ENDPOINT)

        self._autopilot = Autopilote()
        self._pose_smoother = PoseSmoother()
        self._heading_estimator = _HeadingEstimator()

        self._navigation_enabled = bool(WAYPOINTS)
        self._wp_index = 0

        self._cmd_log_writer: Optional[csv.DictWriter] = None
        self._cmd_log_file = None
        self._laboratory: Optional[Laboratory] = None

        if self._navigation_enabled:
            print(f"Керування увімкнено: {len(WAYPOINTS)} точок маршруту, команди -> {CMD_ENDPOINT}")
            LOG_DIR.mkdir(parents=True, exist_ok=True)
            cmd_log_path = LOG_DIR / f"nav_commands_{time.strftime('%Y%m%d_%H%M%S')}.csv"
            self._cmd_log_file = cmd_log_path.open("w", newline="", encoding="utf-8")
            self._cmd_log_writer = csv.DictWriter(self._cmd_log_file, fieldnames=[
                "t_s", "waypoint_index", "target_lat", "target_lon", "target_alt_m",
                "roll_cmd_deg", "pitch_cmd_deg", "yaw_rate_cmd_deg_s", "thrust_cmd",
                "have_fix", "drone_lat", "drone_lon", "drone_alt_m",
                "dist_to_target_m", "heading_deg", "bearing_deg", "status",
            ])
            self._cmd_log_writer.writeheader()
            print(f"Лог команд керування: {cmd_log_path}")

            self._laboratory = Laboratory()
            print("Вікно карти відкрито (окреме Tkinter-вікно).")
        else:
            print("WAYPOINTS порожній — керування вимкнено, скрипт лише спостерігає.")

        self._t_start = time.perf_counter()

        self._latest_landmarks: list[Landmark] = []
        # Опорна точка для локальної ENU-площини — фіксується один раз, по першому
        # отриманому списку орієнтирів, і більше не змінюється.
        self._ref_lat: Optional[float] = None
        self._ref_lon: Optional[float] = None
        self._latest_real_pose: Optional[Position] = None

        self._video_fps = 0.0
        self._last_frame_time = time.perf_counter()

    # ── Головний цикл ────────────────────────────────────────────────────

    def run(self) -> None:
        print(f"Підключено до {ZMQ_ENDPOINT}. Очікування кадрів з камери... ('q' у вікні відео — вихід)")
        try:
            while True:
                if not self._sensor_socket.poll(timeout=1000):
                    continue

                parts = self._sensor_socket.recv_multipart()
                if len(parts) < 2:
                    continue

                frame, landmarks, real_pose = process_envelope(parts)
                if landmarks is not None:
                    self._latest_landmarks = landmarks
                    if self._ref_lat is None and landmarks:
                        self._ref_lat = sum(lm.latitude for lm in landmarks) / len(landmarks)
                        self._ref_lon = sum(lm.longitude for lm in landmarks) / len(landmarks)
                        print(f"Опорна точка ENU зафіксована: lat={self._ref_lat:.6f} lon={self._ref_lon:.6f}")
                if real_pose is not None:
                    self._latest_real_pose = real_pose

                if frame is None:
                    continue

                now = time.perf_counter()
                dt = now - self._last_frame_time
                self._last_frame_time = now
                if dt > 0:
                    instant_fps = 1.0 / dt
                    self._video_fps = instant_fps if self._video_fps == 0.0 else self._video_fps * 0.9 + instant_fps * 0.1

                matches = self._pipeline.tick(frame, self._latest_landmarks)

                drone_pose: Optional[Position] = None
                if self._ref_lat is not None:
                    raw_pose = estimate_drone_position(matches, self._ref_lat, self._ref_lon)
                    if raw_pose is not None:
                        drone_pose = self._pose_smoother.update(raw_pose, now)

                cmd: Optional[ControlCommand] = None
                if self._navigation_enabled:
                    # Autopilote керує за РЕАЛЬНОЮ позицією дрона (ZMQ-сенсор
                    # drone_geo_position), а не за PnP-розрахованою з зображення —
                    # drone_pose лишається тільки для відображення/звірки на карті.
                    cmd = self._navigate(self._latest_real_pose, now, frame)
                    self._send_command(cmd)
                    self._log_command(cmd, self._latest_real_pose)
                    if self._laboratory is not None:
                        self._laboratory.update(drone_pose, self._latest_real_pose, WAYPOINTS, self._current_target())

                vis = draw_and_report(
                    frame, matches, len(self._latest_landmarks), drone_pose,
                    cmd.status if cmd is not None else None,
                    self._class_names, self._video_fps, self._pipeline.last_latency_ms,
                )

                cv2.imshow("UAV object geolocation", vis)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break

        except KeyboardInterrupt:
            print("Зупинено користувачем.")
        finally:
            self.close()

    # ── Навігація: троттлений курс, перемикання точки, виклик Autopilote ────

    def _navigate(self, real_pose: Optional[Position], now: float, frame: np.ndarray) -> ControlCommand:
        if self._ref_lat is None:
            return ControlCommand(0.0, 0.0, 0.0, CRUISE_THRUST,
                                   "waiting for reference point (no landmarks seen yet)")

        if real_pose is not None:
            heading_deg = self._heading_estimator.update(real_pose, self._ref_lat, self._ref_lon, now)
            self._advance_waypoint_if_reached(real_pose)
        else:
            heading_deg = self._heading_estimator.heading_deg

        current_target = self._current_target()
        if current_target is None:
            return ControlCommand(0.0, 0.0, 0.0, CRUISE_THRUST, "mission complete")

        return self._autopilot.compute_command(real_pose, current_target)

    def _current_target(self) -> Optional[Position]:
        return None if self._wp_index >= len(WAYPOINTS) else WAYPOINTS[self._wp_index]

    def _advance_waypoint_if_reached(self, real_pose: Position) -> None:
        target = self._current_target()
        if target is None:
            return
        target_lat, target_lon, target_alt = target
        lat, lon, alt = real_pose
        d_east, d_north = latlon_to_local_m(target_lat, target_lon, lat, lon)
        dist_m = math.sqrt(d_east ** 2 + d_north ** 2 + (target_alt - alt) ** 2)
        if dist_m <= WAYPOINT_RADIUS_M:
            print(f"[NAV] Точку {self._wp_index + 1}/{len(WAYPOINTS)} досягнуто "
                  f"(відстань {dist_m:.1f} м <= {WAYPOINT_RADIUS_M:.0f} м)")
            self._wp_index += 1

    def _send_command(self, cmd: ControlCommand) -> None:
        try:
            self._cmd_socket.send_json(
                build_command(cmd.roll_deg, cmd.pitch_deg, cmd.yaw_rate_deg_s, cmd.thrust),
                flags=zmq.NOBLOCK,
            )
        except zmq.Again:
            pass
        print(f"[CMD] roll={cmd.roll_deg:+6.1f} pitch={cmd.pitch_deg:+6.1f} "
              f"yaw_rate={cmd.yaw_rate_deg_s:+5.1f} thrust={cmd.thrust:.2f}  | {cmd.status}")

    def _log_command(self, cmd: ControlCommand, drone_pose: Optional[Position]) -> None:
        if self._cmd_log_writer is None:
            return
        dp_lat, dp_lon, dp_alt = drone_pose if drone_pose is not None else (None, None, None)
        target = self._current_target()
        self._cmd_log_writer.writerow({
            "t_s": f"{time.perf_counter() - self._t_start:.3f}",
            "waypoint_index": self._wp_index if self._wp_index < len(WAYPOINTS) else None,
            "target_lat": target[0] if target is not None else None,
            "target_lon": target[1] if target is not None else None,
            "target_alt_m": target[2] if target is not None else None,
            "roll_cmd_deg": f"{cmd.roll_deg:.3f}", "pitch_cmd_deg": f"{cmd.pitch_deg:.3f}",
            "yaw_rate_cmd_deg_s": f"{cmd.yaw_rate_deg_s:.3f}", "thrust_cmd": f"{cmd.thrust:.3f}",
            "have_fix": int(drone_pose is not None),
            "drone_lat": dp_lat, "drone_lon": dp_lon, "drone_alt_m": dp_alt,
            "dist_to_target_m": cmd.dist_m, "heading_deg": cmd.heading_deg,
            "bearing_deg": cmd.bearing_deg, "status": cmd.status,
        })
        self._cmd_log_file.flush()

    # ── Завершення роботи ────────────────────────────────────────────────

    def close(self) -> None:
        if self._navigation_enabled:
            # Плавний вихід у нейтраль.
            for _ in range(5):
                try:
                    self._cmd_socket.send_json(build_command(0.0, 0.0, 0.0, CRUISE_THRUST), flags=zmq.NOBLOCK)
                except zmq.Again:
                    pass
                time.sleep(0.02)
        self._pipeline.close()
        self._cmd_socket.close(0)
        self._sensor_socket.close()
        self._zmq_context.term()
        cv2.destroyAllWindows()
        if self._cmd_log_file is not None:
            self._cmd_log_file.close()
        if self._laboratory is not None:
            self._laboratory.close()


if __name__ == "__main__":
    UavFlyTest().run()
