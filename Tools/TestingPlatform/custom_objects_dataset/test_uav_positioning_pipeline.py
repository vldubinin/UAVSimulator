"""
Тест закритого циклу візуального позиціювання БПЛА за методикою зі статті
"Simulation Platform for Sensor-Data-Driven Testing and Closed-Loop Control
of UAV Algorithms" (розд. 3.6, "System Workflow in Action").

Цикл на кожен кадр (розд. 3.6.4):
  1. Кадр з бортової камери (images/train/*.jpg).
  2. Детекція орієнтирів нейромережею (YOLO, вже натренована на цьому датасеті).
  3. Зіставлення 2D-детекцій з відомими geo-координатами орієнтирів
     (virtual_map.json + labels/train/*.meta.json) і визначення власної
     позиції БПЛА через PnP (камера відкалібрована: fx=fy=320px, cx=320,
     cy=240 — з HFOV=90°/VFOV=73.74° при 640x480, дисторсія відсутня).

Метрики (розд. 3.6.6): RMSE позиції відносно еталонної телеметрії
(exp_geo_position.txt), середнє відхилення траєкторії, латентність кадру.

Під час прогону (SHOW_VISUALIZATION=True, за замовчуванням) відкриваються
два вікна: відеоряд бортової камери з відмальованими детекціями/зіставленими
орієнтирами, і вікно з динамічними графіками (траєкторія зверху, похибка
позиції та латентність у часі) — оновлюються по мірі обробки кадрів.
Натискання 'q' у вікні відео зупиняє прогін достроково (метрики рахуються
по вже оброблених кадрах). SHOW_VISUALIZATION=False вимикає обидва вікна —
тест лишається придатним для headless/CI-запуску.

Запуск: `python test_uav_positioning_pipeline.py` (з будь-якої директорії —
шляхи резолвяться відносно цього файлу). Ненульовий exit code при провалі
структурних перевірок датасету або ознаках зламаного пайплайна позиціювання.
"""

from __future__ import annotations

import json
import math
import time
from pathlib import Path

import cv2
import matplotlib.pyplot as plt
import numpy as np
from ultralytics import YOLO

# ── Конфігурація ────────────────────────────────────────────────────────────

SHOW_VISUALIZATION: bool = True  # відеоряд + динамічні графіки; False — headless-прогін

DATASET_DIR = Path(__file__).resolve().parent
IMAGES_DIR = DATASET_DIR / "images" / "train"
LABELS_DIR = DATASET_DIR / "labels" / "train"
VIRTUAL_MAP_PATH = DATASET_DIR / "virtual_map.json"
GEO_POSITION_PATH = DATASET_DIR / "exp_geo_position.txt"
DATA_YAML_PATH = DATASET_DIR / "data.yaml"
WEIGHTS_PATH = DATASET_DIR / "full-set-best.pt"

# Внутрішні параметри камери (з логу симулятора): resolution=640x480,
# HFOV=90.00° VFOV=73.74° focal=320.00px. Перевірка: 320 = (640/2)/tan(45°)
# і 320 = (480/2)/tan(73.74°/2) — узгоджено, тож fx=fy=320, головна точка —
# у центрі кадру, дисторсія відсутня (симульована камера, не реальна оптика).
IMG_W, IMG_H = 640, 480
FX = FY = 320.0
CX, CY = IMG_W / 2.0, IMG_H / 2.0
CAMERA_MATRIX = np.array([[FX, 0, CX], [0, FY, CY], [0, 0, 1]], dtype=np.float64)
DIST_COEFFS = np.zeros(4, dtype=np.float64)

DEG_M = 111_320.0  # метрів на градус широти (наближення, достатнє для масштабу датасету)
MIN_PNP_POINTS = 4
MATCH_MAX_DIST_FRAC = 0.15  # поріг зіставлення детекції з орієнтиром: частка діагоналі кадру

# Bbox, що торкається краю кадру, — наслідок часткового виходу об'єкта з поля
# зору: видно лише частину об'єкта, тож вимірюваний центр bbox зміщений
# відносно проєкції справжньої geo-точки об'єкта (систематичний зсув, а не
# шум). LandmarkTracker підставляє замість такого виміру прогноз фільтра
# Калмана (екстраполяцію тренду руху цього ж orientира в попередніх кадрах).
BORDER_MARGIN_PX = 2.0
MAX_TRACK_GAP_FRAMES = 3  # якщо orientир не бачили довше — трек вважається перерваним, фільтр стартує заново

EARTH_R = 6_371_000.0


# ── Геодезичні хелпери ──────────────────────────────────────────────────────

def latlon_to_local_m(lat: float, lon: float, ref_lat: float, ref_lon: float) -> tuple[float, float]:
    """Рівнокутна ENU-апроксимація: (схід, північ) у метрах відносно опорної точки."""
    east = (lon - ref_lon) * math.cos(math.radians(ref_lat)) * DEG_M
    north = (lat - ref_lat) * DEG_M
    return east, north


def local_m_to_latlon(east: float, north: float, ref_lat: float, ref_lon: float) -> tuple[float, float]:
    lat = ref_lat + north / DEG_M
    lon = ref_lon + east / (math.cos(math.radians(ref_lat)) * DEG_M)
    return lat, lon


def haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlmb = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dlmb / 2) ** 2
    return 2 * EARTH_R * math.asin(math.sqrt(a))


def point_to_polyline_dist_m(point_en: tuple[float, float], polyline_en: list[tuple[float, float]]) -> float:
    """Найменша перпендикулярна відстань (у метрах, локальна площина east/north) від точки до полілінії."""
    px, py = point_en
    best = math.inf
    for (ax, ay), (bx, by) in zip(polyline_en, polyline_en[1:]):
        dx, dy = bx - ax, by - ay
        seg_len2 = dx * dx + dy * dy
        if seg_len2 == 0:
            t = 0.0
        else:
            t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / seg_len2))
        cx_, cy_ = ax + t * dx, ay + t * dy
        d = math.hypot(px - cx_, py - cy_)
        best = min(best, d)
    return best


# ── Завантаження датасету ───────────────────────────────────────────────────

def load_frames() -> list[str]:
    names = sorted(p.stem for p in IMAGES_DIR.glob("*.jpg"))
    assert names, f"Немає жодного кадру в {IMAGES_DIR}"
    return names


def load_meta(name: str) -> dict:
    path = LABELS_DIR / f"{name}.meta.json"
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_label_lines(name: str) -> list[str]:
    path = LABELS_DIR / f"{name}.txt"
    if not path.exists():
        return []
    with open(path, "r", encoding="utf-8") as f:
        return [ln for ln in f.read().splitlines() if ln.strip()]


def load_virtual_map() -> dict:
    with open(VIRTUAL_MAP_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def load_geo_reference() -> dict[str, tuple[float, float, float]]:
    ref = {}
    with open(GEO_POSITION_PATH, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) != 4:
                continue
            name, lat, lon, alt = parts
            ref[name] = (float(lat), float(lon), float(alt))
    return ref


def load_class_names() -> dict[int, str]:
    names: dict[int, str] = {}
    with open(DATA_YAML_PATH, "r", encoding="utf-8") as f:
        in_names = False
        for line in f:
            if line.strip() == "names:":
                in_names = True
                continue
            if in_names:
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    break
                idx_str, _, name = stripped.partition(":")
                names[int(idx_str)] = name.strip()
    assert names, f"Не вдалося розпарсити секцію names: у {DATA_YAML_PATH}"
    return names


# ── Структурні перевірки датасету (розд. 3.3 / Table 4) ────────────────────

def check_dataset_structure(frames: list[str], class_names: dict[int, str]) -> None:
    for name in frames:
        meta = load_meta(name)
        assert meta["image"] == f"{name}.jpg", f"{name}: meta.image != {name}.jpg"

        lines = load_label_lines(name)
        objects = meta["objects"]
        assert len(lines) == len(objects), (
            f"{name}: {len(lines)} рядків у .txt, але {len(objects)} об'єктів у .meta.json"
        )

        for line, obj in zip(lines, objects):
            cls_str, xc_str, yc_str, w_str, h_str = line.split()
            cls = int(cls_str)
            xc, yc, w, h = float(xc_str), float(yc_str), float(w_str), float(h_str)

            assert cls in class_names, f"{name}: клас {cls} відсутній у data.yaml names"
            assert cls == obj["class"], f"{name}: клас .txt ({cls}) != клас .meta.json ({obj['class']})"
            for v in (xc, yc, w, h):
                assert 0.0 <= v <= 1.0, f"{name}: значення bbox {v} поза [0,1]"

            x1, y1, x2, y2 = obj["bbox_px"]
            exp_xc = (x1 + x2) / 2.0 / IMG_W
            exp_yc = (y1 + y2) / 2.0 / IMG_H
            exp_w = (x2 - x1) / IMG_W
            exp_h = (y2 - y1) / IMG_H
            for got, exp, label in ((xc, exp_xc, "xc"), (yc, exp_yc, "yc"), (w, exp_w, "w"), (h, exp_h, "h")):
                assert abs(got - exp) < 1e-3, (
                    f"{name}: {label}={got} не відповідає bbox_px-реконструкції {exp}"
                )


def check_reference_telemetry(frames: list[str], virtual_map: dict, geo_ref: dict) -> None:
    for name in frames:
        assert name in geo_ref, f"{name}: відсутній запис у exp_geo_position.txt"
        lat, lon, alt = geo_ref[name]
        assert all(math.isfinite(v) for v in (lat, lon, alt)), f"{name}: не-скінченне значення телеметрії"

        meta = load_meta(name)
        for obj in meta["objects"]:
            assert obj["id"] in virtual_map, f"{name}: id {obj['id']!r} відсутній у virtual_map.json"


# ── Асоціація детекцій з відомими орієнтирами ───────────────────────────────

def bbox_center(xyxy) -> tuple[float, float]:
    x1, y1, x2, y2 = xyxy
    return (x1 + x2) / 2.0, (y1 + y2) / 2.0


def touches_frame_border(xyxy: tuple[float, float, float, float], margin: float = BORDER_MARGIN_PX) -> bool:
    x1, y1, x2, y2 = xyxy
    return x1 <= margin or y1 <= margin or x2 >= IMG_W - margin or y2 >= IMG_H - margin


def match_detections_to_landmarks(
    detections: list[tuple[int, tuple[float, float, float, float]]],
    meta_objects: list[dict],
) -> list[tuple[tuple[float, float], tuple[float, float, float, float], dict]]:
    """
    Зіставляє детекції з відомими об'єктами кадру за відстанню між центрами
    bbox (не IoU): bbox_px у .meta.json навмисно збільшений BBOX_SCALE_FACTOR=3
    при генерації датасету (компенсація ефекту foreshortening в Unreal — див.
    collect_custom_objects_dataset.py), тож IoU із "сирим" детекторним боксом
    буде заниженим навіть при правильному влучанні; відстань між центрами
    цьому не підвладна. Кожен орієнтир використовується не більше одного разу.
    Повертає (center, xyxy, obj) — xyxy лишається для перевірки на дотик до
    краю кадру (touches_frame_border) у LandmarkTracker.
    """
    max_dist = MATCH_MAX_DIST_FRAC * math.hypot(IMG_W, IMG_H)
    remaining = list(meta_objects)
    matches: list[tuple[tuple[float, float], tuple[float, float, float, float], dict]] = []

    for cls, xyxy in detections:
        center = bbox_center(xyxy)
        best_obj, best_dist = None, math.inf
        for obj in remaining:
            if obj["class"] != cls:
                continue
            ox1, oy1, ox2, oy2 = obj["bbox_px"]
            ocenter = ((ox1 + ox2) / 2.0, (oy1 + oy2) / 2.0)
            d = math.hypot(center[0] - ocenter[0], center[1] - ocenter[1])
            if d < best_dist:
                best_dist, best_obj = d, obj
        if best_obj is not None and best_dist <= max_dist:
            matches.append((center, xyxy, best_obj))
            remaining.remove(best_obj)

    return matches


# ── PnP-оцінка власної позиції (розд. 3.6.4, крок 3) ───────────────────────

def estimate_pose(
    matches: list[tuple[tuple[float, float], dict]], ref_lat: float, ref_lon: float
) -> tuple[float, float, float] | None:
    if len(matches) < MIN_PNP_POINTS:
        return None

    image_pts = np.array([m[0] for m in matches], dtype=np.float64)
    world_pts = []
    for _, obj in matches:
        east, north = latlon_to_local_m(obj["latitude"], obj["longitude"], ref_lat, ref_lon)
        world_pts.append((east, north, obj["altitude"]))
    world_pts = np.array(world_pts, dtype=np.float64)

    ok, rvec, tvec = cv2.solvePnP(
        world_pts, image_pts, CAMERA_MATRIX, DIST_COEFFS, flags=cv2.SOLVEPNP_EPNP
    )
    if not ok:
        return None

    R, _ = cv2.Rodrigues(rvec)
    cam_pos = (-R.T @ tvec).flatten()  # позиція камери у світових (east, north, up) координатах
    lat, lon = local_m_to_latlon(cam_pos[0], cam_pos[1], ref_lat, ref_lon)
    alt = cam_pos[2]
    if not all(math.isfinite(v) for v in (lat, lon, alt)):
        return None
    return lat, lon, alt


# ── Жива візуалізація: відеоряд + динамічні графіки ─────────────────────────

class LiveVisualizer:
    """
    OpenCV window with the video feed (detections + matched landmarks
    overlaid on the frame) and a matplotlib window with three plots that
    update as frames are processed: trajectory from above (east/north,
    reference vs. estimate), position error over time, detection latency
    over time.
    """

    def __init__(self, reference_track_en: list[tuple[float, float]]):
        cv2.namedWindow("UAV onboard camera", cv2.WINDOW_NORMAL)
        cv2.resizeWindow("UAV onboard camera", IMG_W, IMG_H)

        plt.ion()
        self.fig, (self.ax_traj, self.ax_err, self.ax_lat) = plt.subplots(1, 3, figsize=(15, 4.5))
        self.fig.canvas.manager.set_window_title("UAV positioning — live metrics")

        ref_e = [p[0] for p in reference_track_en]
        ref_n = [p[1] for p in reference_track_en]
        self.ax_traj.plot(ref_e, ref_n, color="gray", linewidth=1.5, label="reference trajectory")
        (self.est_line,) = self.ax_traj.plot([], [], "b-", linewidth=1.2, label="PnP estimate")
        (self.est_current,) = self.ax_traj.plot([], [], "bo", markersize=7, label="current position")
        self.ax_traj.set_title("Trajectory (east/north, m)")
        self.ax_traj.set_xlabel("east, m")
        self.ax_traj.set_ylabel("north, m")
        self.ax_traj.axis("equal")
        self.ax_traj.legend(loc="upper right", fontsize=7)

        (self.err_line,) = self.ax_err.plot([], [], "m-", linewidth=1.2)
        self.ax_err.set_title("Position error per frame")
        self.ax_err.set_xlabel("frame index")
        self.ax_err.set_ylabel("position error, m")

        (self.lat_line,) = self.ax_lat.plot([], [], "g.-", markersize=3, linewidth=0.8)
        self.ax_lat.set_title("Detection latency per frame")
        self.ax_lat.set_xlabel("frame index")
        self.ax_lat.set_ylabel("latency, ms")

        self.fig.tight_layout()

        self.est_e: list[float] = []
        self.est_n: list[float] = []
        self.err_x: list[int] = []
        self.err_y: list[float] = []
        self.lat_x: list[int] = []
        self.lat_y: list[float] = []

    def update(
        self,
        frame_idx: int,
        name: str,
        image: np.ndarray,
        detections: list[tuple[int, tuple[float, float, float, float]]],
        matches: list[tuple[tuple[float, float], tuple[float, float, float, float], dict]],
        corrected_by_center: dict[tuple[float, float], tuple[bool, tuple[float, float]]],
        estimate_en: tuple[float, float] | None,
        pos_error_m: float | None,
        traj_dev_m: float | None,
        latency_ms: float,
    ) -> bool:
        """
        Updates both windows. Rectangle color: yellow — detection not
        matched to a known landmark; green — matched, measurement trusted;
        cyan — matched, but the bbox was deformed by the frame border
        (touches_frame_border), so the CSRT tracker's position was
        substituted for PnP (marked with a cross). Returns False if 'q' was
        pressed.
        """
        matched_centers = {m[0] for m in matches}
        csrt_count = sum(1 for was_corrected, _ in corrected_by_center.values() if was_corrected)
        vis = image.copy()
        for cls, (x1, y1, x2, y2) in detections:
            center = bbox_center((x1, y1, x2, y2))
            if center in matched_centers:
                was_corrected, corrected_center = corrected_by_center[center]
                color = (255, 255, 0) if was_corrected else (0, 255, 0)
            else:
                color = (0, 255, 255)
            cv2.rectangle(vis, (int(x1), int(y1)), (int(x2), int(y2)), color, 2)
            if center in matched_centers and corrected_by_center[center][0]:
                kx, ky = corrected_by_center[center][1]
                cv2.drawMarker(vis, (int(kx), int(ky)), (255, 255, 0),
                                markerType=cv2.MARKER_TILTED_CROSS, markerSize=10, thickness=2)
        cv2.putText(vis, f"{name}  det={len(detections)} matched={len(matches)} csrt={csrt_count}",
                    (8, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
        cv2.putText(vis, f"latency: {latency_ms:.1f} ms",
                    (8, 38), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
        cv2.imshow("UAV onboard camera", vis)

        self.lat_x.append(frame_idx)
        self.lat_y.append(latency_ms)
        self.lat_line.set_data(self.lat_x, self.lat_y)

        if estimate_en is not None:
            self.est_e.append(estimate_en[0])
            self.est_n.append(estimate_en[1])
            self.est_line.set_data(self.est_e, self.est_n)
            self.est_current.set_data([estimate_en[0]], [estimate_en[1]])

            self.err_x.append(frame_idx)
            self.err_y.append(pos_error_m)
            self.err_line.set_data(self.err_x, self.err_y)

        for ax in (self.ax_traj, self.ax_err, self.ax_lat):
            ax.relim()
            ax.autoscale_view()
        self.fig.canvas.draw_idle()
        self.fig.canvas.flush_events()
        plt.pause(0.001)

        key = cv2.waitKey(1) & 0xFF
        return key != ord("q")

    def close(self) -> None:
        cv2.destroyAllWindows()
        plt.ioff()
        plt.close(self.fig)


# ── Трекінг орієнтирів через CSRT (компенсація деформації bbox краєм кадру) ─

class LandmarkTracker:
    """
    Утримує по одному CSRT-трекеру на кожен orientир (ключ — obj["id"]).
    Поки bbox orientира НЕ деформований (touches_frame_border=False) —
    детекція вважається "чистою", і трекер щоразу переініціалізується на
    цьому bbox (стежить за останнім достовірним виглядом об'єкта; трекер
    ніколи не встигає "постаріти" між чистими кадрами). Щойно bbox починає
    деформуватися частковим виходом за межі кадру, замість перерахунку
    центру з деформованого bbox викликається CSRT-трекер: він шукає в
    поточному кадрі положення того самого патча за кореляцією ознак (а не
    просто min/max видимих пікселів, як bbox-детектор), тож менш чутливий
    до обрізання краєм кадру. Якщо orientир не спостерігався довше
    MAX_TRACK_GAP_FRAMES кадрів або CSRT втратив ціль — трек вважається
    перерваним, підставити нічим — використовується сирий (можливо зміщений)
    центр детекції як останній доступний варіант.
    """

    def __init__(self):
        self._trackers: dict[str, "cv2.Tracker"] = {}
        self._last_seen: dict[str, int] = {}

    def update(
        self,
        frame_idx: int,
        obj_id: str,
        image: np.ndarray,
        raw_center: tuple[float, float],
        xyxy: tuple[float, float, float, float],
        is_deformed: bool,
    ) -> tuple[tuple[float, float], bool]:
        """Повертає (точка для PnP, чи була підставлена CSRT-трекером)."""
        gap = frame_idx - self._last_seen.get(obj_id, -10 ** 9)
        has_track = obj_id in self._trackers and gap <= MAX_TRACK_GAP_FRAMES
        self._last_seen[obj_id] = frame_idx

        if not is_deformed:
            # Чистий bbox — (пере)ініціалізуємо трекер на ньому: це наш "золотий" зразок.
            x1, y1, x2, y2 = xyxy
            xi, yi, wi, hi = int(x1), int(y1), int(x2 - x1), int(y2 - y1)
            if wi > 0 and hi > 0:
                tracker = cv2.TrackerCSRT_create()
                tracker.init(image, (xi, yi, wi, hi))
                self._trackers[obj_id] = tracker
            return raw_center, False

        if not has_track:
            return raw_center, False  # деформований з першого спостереження — підставити нічим

        ok, box = self._trackers[obj_id].update(image)
        if not ok:
            del self._trackers[obj_id]
            return raw_center, False

        x, y, w, h = box
        tracked_center = (x + w / 2.0, y + h / 2.0)
        return tracked_center, True


# ── Основний тестовий цикл (розд. 3.6.4 + метрики розд. 3.6.6) ─────────────

def compute_reference_track(
    frames: list[str], virtual_map: dict, geo_ref: dict
) -> tuple[float, float, list[tuple[float, float]]]:
    """Опорна точка (середнє по virtual_map.json) + еталонна траєкторія в локальних east/north метрах."""
    ref_lat = sum(v["latitude"] for v in virtual_map.values()) / len(virtual_map)
    ref_lon = sum(v["longitude"] for v in virtual_map.values()) / len(virtual_map)
    reference_track_en = [
        latlon_to_local_m(geo_ref[name][0], geo_ref[name][1], ref_lat, ref_lon) for name in frames
    ]
    return ref_lat, ref_lon, reference_track_en


def run_positioning_cycle(
    frames: list[str],
    model: YOLO,
    virtual_map: dict,
    geo_ref: dict,
    visualizer: "LiveVisualizer | None" = None,
) -> dict:
    ref_lat, ref_lon, reference_track_en = compute_reference_track(frames, virtual_map, geo_ref)

    pos_errors_m: list[float] = []
    trajectory_dev_m: list[float] = []
    latencies_ms: list[float] = []
    frames_with_estimate = 0
    frames_processed = 0
    csrt_corrections_total = 0
    matches_total = 0
    tracker = LandmarkTracker()

    for frame_idx, name in enumerate(frames):
        image = cv2.imread(str(IMAGES_DIR / f"{name}.jpg"))
        assert image is not None, f"Не вдалося прочитати {name}.jpg"

        t0 = time.perf_counter()
        result = model.predict(image, verbose=False)[0]
        latency_ms = (time.perf_counter() - t0) * 1000.0
        latencies_ms.append(latency_ms)

        detections = [
            (int(cls), tuple(xyxy))
            for cls, xyxy in zip(result.boxes.cls.tolist(), result.boxes.xyxy.tolist())
        ]

        meta = load_meta(name)
        raw_matches = match_detections_to_landmarks(detections, meta["objects"])

        pnp_matches: list[tuple[tuple[float, float], dict]] = []
        corrected_by_center: dict[tuple[float, float], tuple[bool, tuple[float, float]]] = {}
        for raw_center, xyxy, obj in raw_matches:
            corrected_center, was_corrected = tracker.update(
                frame_idx, obj["id"], image, raw_center, xyxy, touches_frame_border(xyxy)
            )
            pnp_matches.append((corrected_center, obj))
            corrected_by_center[raw_center] = (was_corrected, corrected_center)
            if was_corrected:
                csrt_corrections_total += 1
        matches_total += len(raw_matches)

        estimate = estimate_pose(pnp_matches, ref_lat, ref_lon)

        frames_processed += 1
        est_en = None
        pos_error_m = None
        traj_dev_m = None

        if estimate is not None:
            frames_with_estimate += 1
            est_lat, est_lon, est_alt = estimate
            ref_lat_frame, ref_lon_frame, ref_alt_frame = geo_ref[name]

            horiz_err = haversine_m(est_lat, est_lon, ref_lat_frame, ref_lon_frame)
            vert_err = est_alt - ref_alt_frame
            pos_error_m = math.hypot(horiz_err, vert_err)
            pos_errors_m.append(pos_error_m)

            est_en = latlon_to_local_m(est_lat, est_lon, ref_lat, ref_lon)
            traj_dev_m = point_to_polyline_dist_m(est_en, reference_track_en)
            trajectory_dev_m.append(traj_dev_m)

        if visualizer is not None:
            keep_going = visualizer.update(
                frame_idx, name, image, detections, raw_matches, corrected_by_center,
                est_en, pos_error_m, traj_dev_m, latency_ms,
            )
            if not keep_going:
                break

    assert frames_processed > 0, "Жодного кадру не було оброблено"
    for values, label in ((pos_errors_m, "pos_errors_m"), (latencies_ms, "latencies_ms")):
        assert all(math.isfinite(v) for v in values), f"Не-скінченне значення у {label}"
    assert frames_with_estimate > 0, (
        "Жоден кадр не дав валідної PnP-оцінки позиції — ознака зламаного пайплайна "
        "(детекції/зіставлення/PnP), а не просто низької точності."
    )

    rmse_m = math.sqrt(sum(e ** 2 for e in pos_errors_m) / len(pos_errors_m))
    mean_traj_dev_m = sum(trajectory_dev_m) / len(trajectory_dev_m)
    latencies_sorted = sorted(latencies_ms)
    p95_idx = min(len(latencies_sorted) - 1, int(0.95 * len(latencies_sorted)))

    report = {
        "frames_total": len(frames),
        "frames_processed": frames_processed,
        "frames_with_estimate": frames_with_estimate,
        "matches_total": matches_total,
        "csrt_corrections_total": csrt_corrections_total,
        "position_rmse_m": rmse_m,
        "mean_trajectory_deviation_m": mean_traj_dev_m,
        "latency_ms_mean": sum(latencies_ms) / len(latencies_ms),
        "latency_ms_median": latencies_sorted[len(latencies_sorted) // 2],
        "latency_ms_p95": latencies_sorted[p95_idx],
        "latency_ms_max": latencies_sorted[-1],
    }
    return report


def print_report(report: dict) -> None:
    coverage = 100.0 * report["frames_with_estimate"] / report["frames_processed"]
    csrt_frac = 100.0 * report["csrt_corrections_total"] / max(1, report["matches_total"])
    early_stop = report["frames_processed"] < report["frames_total"]
    print("\n=== Звіт тестового циклу позиціювання БПЛА (розд. 3.6.6) ===")
    print(f"Кадрів усього в датасеті: {report['frames_total']}")
    print(f"Кадрів оброблено:         {report['frames_processed']}"
          + (" (зупинено достроково через 'q')" if early_stop else ""))
    print(f"Кадрів з PnP-оцінкою:     {report['frames_with_estimate']} ({coverage:.1f}%)")
    print(f"Зіставлень усього:        {report['matches_total']}, "
          f"замінено CSRT-трекером: {report['csrt_corrections_total']} ({csrt_frac:.1f}%)")
    print(f"RMSE позиції:                  {report['position_rmse_m']:.2f} м")
    print(f"Середнє відхилення траєкторії: {report['mean_trajectory_deviation_m']:.2f} м")
    print(f"Латентність (mean/median):{report['latency_ms_mean']:.2f} / {report['latency_ms_median']:.2f} мс")
    print(f"Латентність (p95/max):    {report['latency_ms_p95']:.2f} / {report['latency_ms_max']:.2f} мс")
    print("=" * 60)


# ── Точка входу ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    frames = load_frames()
    class_names = load_class_names()
    virtual_map = load_virtual_map()
    geo_ref = load_geo_reference()

    print(f"Перевірка структури датасету ({len(frames)} кадрів)...")
    check_dataset_structure(frames, class_names)
    check_reference_telemetry(frames, virtual_map, geo_ref)
    print("Структура датасету відповідає розд. 3.3/Table 4 статті — OK")

    print(f"Завантаження моделі: {WEIGHTS_PATH}")
    model = YOLO(str(WEIGHTS_PATH))

    visualizer = None
    if SHOW_VISUALIZATION:
        _, _, reference_track_en = compute_reference_track(frames, virtual_map, geo_ref)
        visualizer = LiveVisualizer(reference_track_en)

    print("Виконання тестового циклу позиціювання (розд. 3.6.4)...")
    try:
        report = run_positioning_cycle(frames, model, virtual_map, geo_ref, visualizer)
    finally:
        if visualizer is not None:
            visualizer.close()
    print_report(report)

    print("OK")
