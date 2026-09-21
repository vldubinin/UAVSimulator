"""
Визначення глобальних координат об'єктів + власної позиції дрона у реальному
часі з відеопотоку симулятора БПЛА (UAVSimulator), який роздається через ZMQ
(SensorBusComponent, tcp://*:5555 — PUB, мультипарт-повідомлення: part 0 =
JSON-конверт {"sensors": [{"topic": ...}, ...]}, part 1..N = сирі payload'и
сенсорів у тому самому порядку).

Використовує сенсори:
  - "camera"         — JPEG-кадр (CameraFrameComponent, 640x480, HFOV=90°,
                        тобто fx=fy=320px, головна точка в центрі кадру,
                        дисторсія відсутня — та сама модель камери, що й у
                        test_uav_positioning_pipeline.py, розд. "Внутрішні
                        параметри камери").
  - "cesium_objects" — {"objects": [{"id","latitude","longitude","altitude",
                        "pixel_x","pixel_y","visible"}, ...]} від
                        CesiumSurroundingsScannerComponent: список орієнтирів,
                        які зараз видно з камери, з їхніми РЕАЛЬНИМИ глобальними
                        координатами (той самий "obj1", "obj2", ... набір id,
                        що й у virtual_map.json).
  - "custom_objects" — той самий формат від CustomSurroundingsScannerComponent
                        (якщо використовується замість/разом із cesium_objects).
  - "drone_geo_position" — {"latitude","longitude","altitude_m"} від
                        GeoPositionDroneComponent: РЕАЛЬНА (симуляційна)
                        позиція дрона — саме вона йде у WaypointNavigator як
                        власна позиція дрона, а також показується на карті
                        зеленою лінією поряд із PnP-оцінкою (синя лінія).
  - "attitude_indicator" — {"roll_deg","pitch_deg","yaw_deg","roll_rate_dps",
                        "pitch_rate_dps","yaw_rate_dps"} від
                        AttitudeIndicatorComponent: РЕАЛЬНА орієнтація корпусу
                        дрона (крен/тангаж/рискання) + кутові швидкості.
                        Використовується лише для показу в інформаційному
                        блоці на відео (рядок "ATT: ..."), керуванням не
                        споживається.

Підхід — той самий, що й у test_uav_positioning_pipeline.py:
  1. Детекції YOLO зіставляються з видимими орієнтирами по близькості центру
     bbox до pixel_x/pixel_y орієнтира (match_detection_to_landmark, аналог
     match_detections_to_landmarks) — координати ОБ'ЄКТІВ беруться напряму з
     орієнтира, без обчислень.
  2. Ті самі зіставлення (2D-піксель <-> відома 3D geo-точка) йдуть у
     cv2.solvePnP (estimate_drone_position, копія логіки estimate_pose) —
     звідси береться ВЛАСНА позиція дрона (позиція камери), потрібно
     щонайменше MIN_PNP_POINTS зіставлень одночасно в кадрі.

YOLO (full-set-best.pt) не блокує відеопотік: інференс запускається у
фоновому потоці, і новий кадр передається в YOLO лише тоді, коли попередній
уже опрацьовано (DetectionPipeline, ThreadPoolExecutor(max_workers=1)). Усі
кадри, що надходять, поки YOLO зайнятий попереднім, просто пропускаються —
на екрані показується останній доступний результат YOLO, "заморожений" до
наступного завершеного інференсу. На екран виводиться швидкість відеопотоку
(fps) та латентність/fps самого YOLO.

Керування дроном (WaypointNavigator) — той самий канал і формат команд, що й
Tools/TestingPlatform/attitude_control/circle_autopilot.py та maneuvers.py:
ZMQ PUSH -> tcp://127.0.0.1:5556, JSON SET_ATTITUDE_TARGET (АБСОЛЮТНІ roll/pitch
у радіанах, yaw_rate, thrust 0..1) — приймає UAttitudeControlComponent
(режим симуляції має бути "Playback and Auto Track"). На відміну від
circle_autopilot.py (телеметрія з штатного сенсора "drone_position"), тут
власна позиція дрона береться з РЕАЛЬНОЇ (симуляційної) geo-позиції сенсора
"drone_geo_position" (GeoPositionDroneComponent). PnP-оцінка з зображення
(estimate_drone_position — по орієнтирах, зіставлених із детекціями YOLO)
керуванням більше не використовується — лишається тільки для звірки на карті
(синя лінія проти зеленої).
Дрон послідовно летить по точках WAYPOINTS (заповнюються вручну нижче);
перехід до наступної точки — лише ВРУЧНУ, по натисканню Enter у вікні відео
(автоматичного зарахування за радіусом немає). У статусі показується поточна
горизонтальна відстань до точки, щоб оператор бачив, коли її натискати.
Курс тримається креном (roll пропорційний помилці курсу — так само, як стала
крена дає віраж у circle_autopilot.py), висота — тим самим одноконтурним PID
(AltitudeHold), що й circle_autopilot.py/maneuvers.py, ПЛЮС захист висоти у
віражі: тяга компенсує втрату підіймальної сили (1/cos(крен)), а якщо літак
усе одно просів нижче цілі більш ніж на TURN_ALT_FLOOR_DROP_M — вмикається
режим відновлення (крен обмежується, максимальне кабрування, повна тяга),
доки висота не повернеться. Без цього на крутих поворотах (крен до
MAX_BANK_DEG=70°) літак зривався у спіральне зниження.

Стабілізація PnP-позиції (виправлення після аналізу перших логів керування,
де курс скакав на сотні градусів між кадрами):
  1. DetectionPipeline зіставляє детекції з орієнтирами САМ, одразу як YOLO
     завершить інференс, і проти знімку орієнтирів (self._dispatch_landmarks),
     зафіксованого в момент відправки того самого кадру в YOLO — а не проти
     "поточних" орієнтирів, чиї pixel_x/pixel_y щотік перепроєктуються з уже
     зміщеної позиції камери. Без цього застояле bbox YOLO могло щотік
     зіставлятися з іншим орієнтиром, підмінюючи 2D-3D відповідності, що йдуть
     у cv2.solvePnP.
  2. PoseSmoother згладжує сирий вихід estimate_drone_position (EMA), перш ніж
     його побачить WaypointNavigator — одиночний solvePnP по невеликій
     кількості точок сам собою шумний, а курс/вертикальна швидкість рахуються
     саме по різниці послідовних позицій.
  3. WaypointNavigator оцінює курс на базі щонайменше HEADING_UPDATE_INTERVAL_S
     секунд (а не між сусідніми кадрами відео) — на короткій базі шум, що
     лишається навіть після PoseSmoother, порівнянний за величиною з реальним
     переміщенням дрона за один тік, тож напрямок виходить майже випадковим
     (підтверджено логом: курс стрибав на сотні градусів за долі секунди).
  4. estimate_drone_position використовує cv2.solvePnPRansac замість "голого"
     solvePnP, а PoseSmoother відкидає сирі фікси з фізично неможливою
     швидкістю (MAX_POSE_SPEED_MPS) — навіть після (1)-(3) поодинокий хибний
     2D-3D збіг усе ще міг "телепортувати" позицію на сотні метрів за один
     кадр (підтверджено логом: ~126 м за 0.055 с).

Карта (MapView) — нативне Tkinter-вікно (tkintermapview, як у
Tools/TestingPlatform/attitude_control/marker/map_object_marker.py) з
рельсами OpenStreetMap, без API-ключа й без локального HTTP-сервера/браузера
(попередній підхід на їхній основі виявився ненадійним). Мітки точок
маршруту — трьох різних кольорів (сіра/очікує, червона/поточна ціль,
зелена/досягнуто); синя лінія + синя мітка — пройдений шлях і поточна
позиція за нашою PnP-оцінкою (ті самі згладжені фікси, що й керування);
зелена лінія — РЕАЛЬНА (симуляційна) позиція дрона з "drone_geo_position"
(GeoPositionDroneComponent), для візуальної звірки оцінки з істиною.
Tkinter не потокобезпечний для довільних викликів з чужого потоку, тож усе
вікно живе у власному фоновому потоці; головний цикл лише кладе оновлення в
чергу (queue.Queue). Потребує інтернету (тайли OSM), але жодного API-ключа.
Встановлення: `pip install tkintermapview`.

Запуск: `python uav_object_geolocation.py` під час активної симуляції з
увімкненим SensorBusComponent (і хоча б одним із cesium_objects/custom_objects
сенсорів), режим симуляції "Playback and Auto Track" (щоб працював
UAttitudeControlComponent). У вікні відео: Enter — зарахувати поточну точку
маршруту й перейти до наступної, 'q' — завершити роботу.
"""

from __future__ import annotations

import csv
import ctypes
import json
import math
import queue
import threading
import time
import tkinter as tk
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

try:
    from tkintermapview import TkinterMapView
except ImportError:
    TkinterMapView = None

import cv2
import numpy as np
import zmq
from ultralytics import YOLO

# ── Конфігурація ────────────────────────────────────────────────────────────

ZMQ_ENDPOINT: str = "tcp://127.0.0.1:5555"  # SUB підключається до PUB симулятора

WEIGHTS_PATH = Path(__file__).resolve().parent / "full-set-best.pt"

# Внутрішні параметри камери — ідентичні до тих, що використані у
# test_uav_positioning_pipeline.py (CameraFrameComponent завжди віддає 640x480,
# HFOV=90° -> fx=fy=320px, головна точка в центрі кадру, дисторсія відсутня).
IMG_W, IMG_H = 640, 480
FX = FY = 320.0
CX, CY = IMG_W / 2.0, IMG_H / 2.0
CAMERA_MATRIX = np.array([[FX, 0, CX], [0, FY, CY], [0, 0, 1]], dtype=np.float64)
DIST_COEFFS = np.zeros(4, dtype=np.float64)

DEG_M = 111_320.0  # метрів на градус широти (наближення, як у test_uav_positioning_pipeline.py)
MIN_PNP_POINTS = 6       # запас понад мінімальні 4 точки EPnP — щоб RANSAC міг відкинути 1-2 хибні
PNP_REPROJECTION_ERROR_PX = 8.0  # поріг inlier'а для cv2.solvePnPRansac, пікселі
MAX_POSE_SPEED_MPS = 60.0  # фізично неможливий "стрибок" сирого PnP-фіксу відкидається
                            # (спостережено стрибки в сотні м/с — залишковий викид навіть із RANSAC)

CONF_THRESHOLD: float = 0.25

# Поріг зіставлення детекції з орієнтиром: частка діагоналі кадру (як
# MATCH_MAX_DIST_FRAC у test_uav_positioning_pipeline.py).
MATCH_MAX_DIST_FRAC: float = 0.15
MATCH_MAX_DIST_PX: float = MATCH_MAX_DIST_FRAC * math.hypot(IMG_W, IMG_H)

# ── Керування (WaypointNavigator) ────────────────────────────────────────────

CMD_ENDPOINT: str = "tcp://127.0.0.1:5556"  # ZMQ PUSH -> PULL у UAttitudeControlComponent

# Ключові точки маршруту: (latitude, longitude, altitude_m). Заповніть самостійно
# (наприклад, координатами орієнтирів із cesium_objects/virtual_map.json або
# точками, зчитаними під час розвідувального польоту). Порожній список = дрон
# не керується, скрипт лише спостерігає (як і раніше).
WAYPOINTS: list[tuple[float, float, float]] = [
    (50.0426466, 36.2802352, 120.0),
    (50.0475773, 36.2905634, 120),
    (50.05302401551346, 36.29828075046079, 120.0),
    (50.0430317, 36.3003301, 200)
]

# Перехід до наступної точки маршруту — тільки вручну, по натисканню Enter у вікні
# відео (WaypointNavigator.advance). Автоматичного зарахування за радіусом немає.
MAX_BANK_DEG: float = 70.0          # обмеження команди крену (було 40° — +15%; більший крен -> менший радіус віражу:
                                     # R = V²/(g·tan(крен)))

# Обов'язково: короткочасна (менше цього) втрата фіксу власної PnP-позиції НЕ
# перериває поточний маневр — WaypointNavigator повторює останню видану команду
# (крен/тангаж/yaw_rate) замість того, щоб вирівнюватись у нуль. Коли фіксу немає
# вже ДОВШЕ MAX_FIX_LOSS_S (байдуже, тривав перед тим поворот чи рівний політ),
# довіри до "останньої відомої" команди вже нема — дрон вирівнюється (крен -> 0) і
# опускає ніс (NOSE_DOWN_RECOVERY_PITCH_DEG), щоб камера дивилась більше вниз і мала
# кращий шанс знову побачити відомі орієнтири. Щойно фікс повертається, звичайна
# логіка update() одразу перераховує пеленг/курс від поточної позиції — окремого коду
# для "повернення до попереднього положення" не треба: ця відновна постава діє лише,
# поки drone_pose є None.
MAX_FIX_LOSS_S: float = 4.0
NOSE_DOWN_RECOVERY_PITCH_DEG: float = -8.0

# Bang-bang: тримати МАКСИМАЛЬНИЙ крен, доки курс не вирівняється з пеленгом на
# ціль у межах ALIGN_THRESHOLD_DEG (найкоротша дуга перехоплення точки при
# обмеженому радіусі віражу — теорія Дубінса), і лише в цьому вузькому коридорі
# лінійно спадати до нуля (щоб не смикати крен туди-сюди рівно на порозі).
# Той самий поріг (обов'язково) визначає й форсований поворот: доки похибка
# курсу перевищує ALIGN_THRESHOLD_DEG, крок 1 (максимальний крен вище) йде
# РАЗОМ із кроком 2 — максимальним тангажем (MAX_PITCH_DEG, нижче) — і так
# тримається, поки похибка курсу знову не стане меншою за ALIGN_THRESHOLD_DEG.
ALIGN_THRESHOLD_DEG: float = 10.0

MAX_YAW_RATE_DEG_S: float = 30.0    # обмеження команди рискання — додаткова, незалежна від крену
                                     # "різкість" повороту (SET_ATTITUDE_TARGET.yaw_rate)
KP_YAW_RATE: float = 1.0            # град/с рискання на град помилки курсу

# Форсований поворот, крок 2 (обов'язково): доки похибка курсу перевищує
# ALIGN_THRESHOLD_DEG, тангаж НЕ рахується через AltitudeHold — він ставиться
# напряму в MAX_PITCH_DEG (максимальне керма висоти) поверх кроку 1
# (максимальний крен, bang-bang вище). AltitudeHold повертається, лише коли
# похибка курсу знову стає меншою за ALIGN_THRESHOLD_DEG.
MAX_PITCH_DEG: float = 24.0  # межа тангажу — і водночас значення "максимального керма висоти" кроку 2

# Запобіжник висоти для кроку 2: форсований підйом (MAX_PITCH_DEG) не діє, якщо дрон
# уже вищий за target_alt більш ніж на цю величину — тоді керування висотою
# повертається до AltitudeHold (і за потреби опускає ніс), навіть якщо похибка курсу
# все ще понад ALIGN_THRESHOLD_DEG. Без цього при нестабільній оцінці курсу (heading
# рідко затримується в межах ALIGN_THRESHOLD_DEG довше частки секунди — спостережено
# в лозі: стрибки heading_deg на 20-160° між сусідніми вимірами) крок 2 залишається
# увімкненим практично безперервно, і дрон набирає висоту без жодної межі.
HARD_TURN_MAX_ALT_ABOVE_TARGET_M: float = 20.0

# Компенсація затримки: heading_deg оновлюється не щотік, а раз на
# HEADING_UPDATE_INTERVAL_S (див. нижче) — тобто на момент розрахунку команди
# він уже трохи застарілий, а дрон весь цей час фактично продовжував
# розвертатися під ОСТАННІЙ виданий крен. HEADING_PREDICTION_ENABLED вмикає
# екстраполяцію (dead reckoning) поточного курсу вперед за формулою
# координованого віражу (turn_rate = g·tan(крен)/V), щоб команда рахувалась
# від курсу, ближчого до реального "зараз", а не від застарілого виміру —
# це зменшує перерегулювання (overshoot) від затримки в контурі керування.
HEADING_PREDICTION_ENABLED: bool = True
MAX_HEADING_PREDICTION_S: float = 1.5  # не екстраполювати далі цього — якщо фіксів
                                        # немає так довго, довіри до прогнозу вже нема
GRAVITY_MPS2: float = 9.81

# Межа самого turn_rate_deg_s з формули координованого віражу (g·tan(крен)/V):
# при малому ground_speed_mps (шумний/перехідний вимір, спостережено ~3 м/с)
# і крені, близькому до MAX_BANK_DEG, формула чисто математично вибухає до
# сотень °/с (спостережено в лозі: ~468 °/с), і навіть частка секунди дає
# екстрапольований курс, що не має нічого спільного з реальним — heading_error
# від такого "прогнозу" був майже випадковим, через що крен щотік перемикався
# -70/+70 (без реального повороту), а тангаж (форсований поворот) застрягав
# у MAX_PITCH_DEG майже без перерви -> неконтрольований набір висоти.
MAX_TURN_RATE_PREDICTION_DEG_S: float = 45.0

CRUISE_THRUST: float = 0.65

# ── Захист висоти у віражі ──────────────────────────────────────────────────
# У координованому віражі вертикальна складова підіймальної сили падає в cos(крен)
# разів, тож без компенсації літак втрачає висоту (спіральне зниження — саме це й
# спостерігалось на крутих поворотах при MAX_BANK_DEG=70°, n=1/cos70°≈2.9g).
# Компенсуємо тягою: базова тяга ділиться на cos(крен) і обмежується TURN_THRUST_MAX.
TURN_THRUST_MAX: float = 1.0
# Якщо навіть із компенсацією тягою літак просів більш ніж на TURN_ALT_FLOOR_DROP_M
# нижче цільової висоти — вмикається режим відновлення висоти: крен примусово
# обмежується TURN_RECOVERY_MAX_BANK_DEG (щоб повернути вертикальну складову
# підіймальної сили), тангаж — максимальне кабрування, тяга — повна. Курс на цей
# час вторинний. Режим вимикається (гістерезис), коли літак підіймається назад у
# межі TURN_ALT_RECOVER_M від цілі.
TURN_ALT_FLOOR_DROP_M: float = 25.0
TURN_ALT_RECOVER_M: float = 10.0
TURN_RECOVERY_MAX_BANK_DEG: float = 25.0

MIN_NAV_DISPLACEMENT_M: float = 2.0  # мінімальне зміщення між PnP-фіксами для оновлення курсу
                                      # (менше — шум PnP, а не реальний рух)
HEADING_UPDATE_INTERVAL_S: float = 0.5  # мінімальний час між семплами для оцінки курсу — на
                                         # короткій базі (сусідні кадри відео, ~0.05-0.1с) шум
                                         # згладженої PnP-позиції (частки метра) порівнянний із
                                         # самим переміщенням, тож напрямок виходить майже
                                         # випадковим; на довшій базі реальний рух домінує над шумом.

LOG_DIR = Path(__file__).resolve().parent / "logs"  # єдиний CSV-лог: рядок на кожен кадр (flight_log_*.csv)

MAP_ZOOM: int = 15
MIN_PATH_POINT_SPACING_M: float = 1.0  # не додавати нову точку треку, поки дрон не відійшов на стільки

# Тайли карти з АНГЛІЙСЬКИМИ назвами вулиць/населених пунктів. Стандартні тайли
# OpenStreetMap підписані місцевою мовою (для Харкова — кирилицею); Google-тайли
# з параметром hl=en віддають ту саму карту з латинськими підписами й не
# потребують API-ключа. {x}/{y}/{z} підставляє tkintermapview.
MAP_TILE_SERVER_EN: str = "https://mt1.google.com/vt/lyrs=m&hl=en&x={x}&y={y}&z={z}"
MAP_TILE_MAX_ZOOM: int = 19


# ── Геодезичні хелпери (та сама ENU-апроксимація, що й у test-скрипті) ──────

def latlon_to_local_m(lat: float, lon: float, ref_lat: float, ref_lon: float) -> tuple[float, float]:
    east = (lon - ref_lon) * math.cos(math.radians(ref_lat)) * DEG_M
    north = (lat - ref_lat) * DEG_M
    return east, north


def local_m_to_latlon(east: float, north: float, ref_lat: float, ref_lon: float) -> tuple[float, float]:
    lat = ref_lat + north / DEG_M
    lon = ref_lon + east / (math.cos(math.radians(ref_lat)) * DEG_M)
    return lat, lon


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
    кадр було відправлено в YOLO. Раніше зіставлення робилося зовні, проти
    "поточних" (найсвіжіших) орієнтирів — а їхні pixel_x/pixel_y
    перепроєктуються з живої, вже зміщеної позиції камери щотік, тоді як bbox
    YOLO лишається замороженим на кадрі кількасекундної давнини. Це відв'язувало
    2D-піксель від 3D-точки, з якою його зіставляли: набір відповідностей, що
    йшов у cv2.solvePnP, міг підмінюватися іншим орієнтиром від тіку до тіку
    навіть без жодного нового результату YOLO — звідси стрибки оціненої позиції
    дрона на десятки метрів і курсу на сотні градусів між сусідніми кадрами.
    """

    def __init__(self, model: YOLO) -> None:
        self._model = model
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="yolo")
        self._pending: Optional[Future] = None
        self._dispatch_landmarks: list[Landmark] = []
        self._last_matches: list[Match] = []
        self.last_latency_ms: Optional[float] = None

    def close(self) -> None:
        self._executor.shutdown(wait=False, cancel_futures=True)

    def tick(self, frame: np.ndarray, landmarks: list[Landmark]) -> list[Match]:
        # Якщо фоновий інференс завершився — забрати результат і латентність,
        # і зіставити ЙОГО детекції з орієнтирами, зафіксованими на момент
        # відправки ЦЬОГО кадру (self._dispatch_landmarks), а не з поточними.
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

        # Якщо YOLO вільний — віддати йому поточний кадр разом зі знімком
        # орієнтирів цього ж моменту; інакше цей кадр для YOLO просто
        # пропускається (буде показано лише на відео).
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


# ── PnP-оцінка власної позиції дрона (копія логіки estimate_pose з test-скрипту) ─

def estimate_drone_position(
    matches: list[Match], ref_lat: float, ref_lon: float
) -> Optional[tuple[float, float, float]]:
    """
    cv2.solvePnPRansac замість "голого" solvePnP: один хибний 2D-3D збіг (неправильно
    зіставлений орієнтир — greedy nearest-neighbour у match_detection_to_landmark не
    застрахований від помилок) здатен зіпсувати ввесь розв'язок EPnP, даючи позицію,
    що "телепортується" на сотні метрів за один кадр (підтверджено логом: стрибок
    ~126 м за 0.055 с — фізично неможлива швидкість). RANSAC підбирає найбільшу
    узгоджену підмножину точок (inliers, поріг PNP_REPROJECTION_ERROR_PX) і рахує
    позу лише по ній, ігноруючи викиди.
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
    відсіюванням фізично неможливих "стрибків" (reject_outliers): навіть після
    RANSAC поодинокий кадр може дати позицію, що маломожливо-швидко "телепортує"
    дрона на сотні метрів (спостережено в лозі: ~126 м за 0.055 с). Такий сирий
    фікс просто ігнорується — повертається попередня згладжена оцінка — замість
    того, щоб на чверть (alpha) протягнути цей викид у прийняту позицію.
    WaypointNavigator рахує курс і вертикальну швидкість по РІЗНИЦІ послідовних
    позицій, тож без цього непомічений викид напряму стає хаотичною командою крену.
    """
    alpha: float = 0.25  # менше -> сильніше згладжування, повільніша реакція на реальний рух
    lat: Optional[float] = None
    lon: Optional[float] = None
    alt: Optional[float] = None
    _last_time: Optional[float] = None

    def update(self, raw: tuple[float, float, float], now: float) -> tuple[float, float, float]:
        raw_lat, raw_lon, raw_alt = raw
        if self.lat is None:
            self.lat, self.lon, self.alt, self._last_time = raw_lat, raw_lon, raw_alt, now
            return self.lat, self.lon, self.alt

        # dt/відстань рахуються від ОСТАННЬОГО ПРИЙНЯТОГО фіксу (self._last_time не
        # оновлюється при відхиленні нижче) — інакше після одного відхилення dt на
        # наступному виклику знову стає малим (~1 кадр), тоді як відстань від
        # застояної self.lat/self.lon продовжує рости з кожним реальним рухом дрона:
        # implied_speed зростала б необмежено і назавжди блокувала б будь-яке
        # наступне оновлення координат.
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


# ── Керування дроном (WaypointNavigator) ─────────────────────────────────────

def _wrap180(angle_deg: float) -> float:
    return (angle_deg + 180.0) % 360.0 - 180.0


def _opt(value: Optional[float], fmt: str = "{:.6f}") -> str:
    """Форматує число для CSV-логу; None -> порожня клітинка."""
    return "" if value is None else fmt.format(value)


@dataclass
class NavCommand:
    """Одна видана команда керування + весь контекст, потрібний для логу."""
    roll_deg: float
    pitch_deg: float
    yaw_rate_deg_s: float
    thrust: float
    status: str
    have_fix: bool
    waypoint_index: Optional[int] = None
    target_lat: Optional[float] = None
    target_lon: Optional[float] = None
    target_alt: Optional[float] = None
    dist_m: Optional[float] = None
    heading_deg: Optional[float] = None
    bearing_deg: Optional[float] = None


def build_command(roll_deg: float, pitch_deg: float, yaw_rate_deg_s: float, thrust: float) -> dict:
    """SET_ATTITUDE_TARGET — кути в РАДІАНАХ (UAttitudeControlComponent порівнює їх
    напряму з поточною орієнтацією без конверсії) — той самий формат, що й у
    circle_autopilot.py / maneuvers.py."""
    return {
        "command_type": "SET_ATTITUDE_TARGET",
        "roll": math.radians(roll_deg),
        "pitch": math.radians(pitch_deg),
        "yaw_rate": math.radians(yaw_rate_deg_s),
        "thrust": float(thrust),
    }


@dataclass
class AltitudeHold:
    """Одноконтурний PID висота -> тангаж — та сама логіка, що й AltitudeHold
    у circle_autopilot.py/maneuvers.py."""
    kp: float = 0.30
    kd: float = 1.50
    ki: float = 0.03
    pitch_min: float = -8.0
    pitch_max: float = 12.0
    integ: float = 0.0

    def update(self, target_alt: float, alt: float, vertical_speed: float, dt: float) -> float:
        err = target_alt - alt  # +ve => нижче цілі => треба кабрувати
        raw = self.kp * err - self.kd * vertical_speed + self.integ
        out = max(self.pitch_min, min(self.pitch_max, raw))
        if out == raw or (err > 0) != (raw > out):  # анти-віндап
            self.integ += self.ki * err * dt
            self.integ = max(self.pitch_min, min(self.pitch_max, self.integ))
        return out


class WaypointNavigator:
    """
    Веде дрон послідовно через WAYPOINTS, спираючись на РЕАЛЬНУ (симуляційну)
    geo-позицію дрона з сенсора "drone_geo_position" (GeoPositionDroneComponent).
    PnP-відеопозиція (estimate_drone_position) керуванням не використовується.

    Перехід до наступної точки маршруту — лише ВРУЧНУ, через advance() (натискання
    Enter у вікні відео). Автоматичного зарахування точки за радіусом немає;
    update() лише рахує пеленг/відстань до current_target і веде дрон на неї,
    доки оператор не натисне Enter.

    Курс тримається bang-bang законом — найкоротша дуга перехоплення точки
    при обмеженому радіусі віражу (теорія Дубінса: за відсутності вимог до
    кінцевого курсу оптимальний шлях до точки — це максимальний віраж, доки
    курс не збігся з пеленгом на ціль, потім пряма лінія). Тому крен завжди
    МАКСИМАЛЬНИЙ (±MAX_BANK_DEG), поки помилка курсу не потрапить у
    ALIGN_THRESHOLD_DEG, і лише в цьому вузькому коридорі спадає до нуля
    лінійно. (Раніше тут стояв плавний L1-закон, як в ArduPilot/PX4,
    a_lat = 2V²/L1·sin(η) — він явно враховував відстань до цілі, але саме
    тому й "розмотував" крен задовго до вирівнювання курсу, даючи ширші, а не
    коротші дуги; для мінімізації самого шляху перельоту, на відміну від
    гладкості проходження довгого маршруту, це не підходить.)
    yaw_rate (SET_ATTITUDE_TARGET.yaw_rate) — додатковий канал доворту курсу
    поверх крену. Висота — тим самим PID (AltitudeHold), що й у
    circle_autopilot.py/maneuvers.py, — АЛЕ ЛИШЕ поки похибка курсу не
    перевищує ALIGN_THRESHOLD_DEG. Форсований поворот (обов'язкова логіка):
    щойно похибка курсу перевищує ALIGN_THRESHOLD_DEG (10°), вмикається
    двокроковий маневр — крок 1: максимальний крен (bang-bang, як вище), крок
    2: максимальний тангаж (MAX_PITCH_DEG) замість виходу AltitudeHold — і
    тримається так, доки похибка курсу знову не стане меншою за
    ALIGN_THRESHOLD_DEG, після чого керування висотою повертається до
    AltitudeHold. Запобіжник: крок 2 не вмикається, якщо дрон уже вищий за
    ціль більш ніж на HARD_TURN_MAX_ALT_ABOVE_TARGET_M — інакше при нестійкій
    оцінці курсу (рідко затримується в межах ALIGN_THRESHOLD_DEG довше частки
    секунди) форсований підйом лишався б увімкненим практично без перерви,
    даючи неконтрольований набір висоти (підтверджено логом).

    Короткочасна втрата фіксу власної позиції (менше MAX_FIX_LOSS_S секунд)
    НЕ перериває поточний маневр — навігатор повторює останню видану команду
    (крен/тангаж/yaw_rate), а не скидає її в нуль (байдуже, тривав перед тим
    поворот чи рівний політ). Коли ж фіксу немає вже ДОВШЕ MAX_FIX_LOSS_S,
    довіри до "останньої відомої" команди вже нема — дрон вирівнюється (крен
    -> 0) і опускає ніс (NOSE_DOWN_RECOVERY_PITCH_DEG), щоб камера мала
    кращий шанс знову побачити відомі орієнтири, і чекає новий фікс. Щойно
    він надійде, звичайна логіка update() автоматично перерахує пеленг/курс
    від поточної позиції — "повернення до попереднього положення" відбувається
    само собою, без окремого коду: ця відновна постава (крен=0,
    pitch=NOSE_DOWN_RECOVERY_PITCH_DEG) діє лише, поки drone_pose є None.

    _predicted_heading() компенсує затримку контуру керування: heading_deg
    вимірюється не щотік, а раз на HEADING_UPDATE_INTERVAL_S (+ латентність
    самого PnP), тож на момент розрахунку дрон, найімовірніше, уже трохи
    довернувся під ОСТАННІЙ виданий крен — команда рахується від курсу,
    екстрапольованого вперед (dead reckoning) за формулою координованого
    віражу turn_rate = g·tan(крен)/V, а не від застарілого виміру.

    Команди йдуть у форматі SET_ATTITUDE_TARGET (build_command) по ZMQ
    PUSH -> CMD_ENDPOINT.
    """

    def __init__(self, waypoints: list[tuple[float, float, float]]) -> None:
        self.waypoints = waypoints
        self.index = 0
        self.altitude_hold = AltitudeHold()
        self.heading_deg: Optional[float] = None       # останній ВИМІРЯНИЙ курс (можливо, вже застарілий)
        self.ground_speed_mps: Optional[float] = None  # для прогнозу курсу вперед (turn_rate = g·tan(крен)/V)

        # Опорна точка для оцінки курсу/швидкості — навмисно НЕ "попередній тік",
        # а точка, зафіксована щонайменше HEADING_UPDATE_INTERVAL_S тому (див. _update_heading).
        self._heading_ref_east: Optional[float] = None
        self._heading_ref_north: Optional[float] = None
        self._heading_ref_time: Optional[float] = None

        self._last_roll_cmd: float = 0.0  # для екстраполяції курсу (HEADING_PREDICTION_ENABLED)

        self._last_alt: Optional[float] = None
        self._last_update_time: Optional[float] = None

        self._last_fix_time: Optional[float] = None      # час останнього ПРИЙНЯТОГО фіксу (для MAX_FIX_LOSS_S)
        self._last_command: Optional[NavCommand] = None  # повторюється при короткій (< MAX_FIX_LOSS_S) втраті фіксу

        self._alt_recovering: bool = False  # режим відновлення висоти у віражі (гістерезис, див. update)

    @property
    def done(self) -> bool:
        return self.index >= len(self.waypoints)

    @property
    def current_target(self) -> Optional[tuple[float, float, float]]:
        return None if self.done else self.waypoints[self.index]

    def advance(self) -> None:
        """Ручний перехід до наступної точки маршруту — викликається по натисканню
        Enter у вікні відео. Автоматичного зарахування за радіусом більше немає."""
        if self.done:
            return
        print(f"[NAV] Точку {self.index + 1}/{len(self.waypoints)} зараховано вручну (Enter)")
        self.index += 1
        if self.done:
            print("[NAV] Усі точки маршруту пройдено.")

    def _update_heading(self, east: float, north: float, now: float) -> None:
        if self._heading_ref_time is None:
            self._heading_ref_east, self._heading_ref_north, self._heading_ref_time = east, north, now
            return

        # Замало часу від попереднього семплу — база ще закоротка, шум домінував би
        # над реальним переміщенням (див. HEADING_UPDATE_INTERVAL_S). Лишаємо стару
        # оцінку курсу і чекаємо накопичення довшої, надійнішої бази.
        elapsed = now - self._heading_ref_time
        if elapsed < HEADING_UPDATE_INTERVAL_S:
            return

        dx, dy = east - self._heading_ref_east, north - self._heading_ref_north
        dist = math.hypot(dx, dy)
        if dist >= MIN_NAV_DISPLACEMENT_M:
            self.heading_deg = math.degrees(math.atan2(dx, dy)) % 360.0
            if elapsed > 1e-3:
                self.ground_speed_mps = dist / elapsed
        self._heading_ref_east, self._heading_ref_north, self._heading_ref_time = east, north, now

    def _predicted_heading(self, now: float) -> Optional[float]:
        """
        Екстраполює self.heading_deg вперед на час, що минув від виміру
        (dead reckoning), за формулою координованого віражу
        turn_rate = g·tan(крен)/V — компенсує затримку контуру керування:
        heading_deg оновлюється не щотік, а раз на HEADING_UPDATE_INTERVAL_S
        (+ додаткова затримка самого вимірювання PnP), тож на момент
        розрахунку команди дрон, найімовірніше, уже трохи довернувся під
        ОСТАННІЙ виданий крен (self._last_roll_cmd) — без компенсації це
        систематично запізнює корекцію й дає перерегулювання (overshoot).
        """
        if self.heading_deg is None:
            return None
        if not HEADING_PREDICTION_ENABLED or self._heading_ref_time is None or self.ground_speed_mps is None:
            return self.heading_deg

        elapsed = min(now - self._heading_ref_time, MAX_HEADING_PREDICTION_S)
        if elapsed <= 0.0 or self.ground_speed_mps < 0.5:
            return self.heading_deg

        turn_rate_deg_s = math.degrees(
            GRAVITY_MPS2 * math.tan(math.radians(self._last_roll_cmd)) / self.ground_speed_mps
        )
        # При малому ground_speed_mps ця формула чисто математично вибухає (поділ на
        # малу швидкість) до фізично неможливих сотень °/с — без цієї межі один
        # шумний семпл швидкості перетворював прогноз курсу на випадкове число.
        turn_rate_deg_s = max(-MAX_TURN_RATE_PREDICTION_DEG_S, min(MAX_TURN_RATE_PREDICTION_DEG_S, turn_rate_deg_s))
        return (self.heading_deg + turn_rate_deg_s * elapsed) % 360.0

    def update(
        self, drone_pose: Optional[tuple[float, float, float]], ref_lat: float, ref_lon: float
    ) -> NavCommand:
        # now рахується по реальному годиннику (не по кадрах відео) — щоб PID висоти й
        # відлік MAX_FIX_LOSS_S лишались коректними, навіть коли кадри з фіксом PnP
        # приходять нерегулярно.
        now = time.perf_counter()

        if self.done:
            return NavCommand(0.0, 0.0, 0.0, CRUISE_THRUST, "mission complete", have_fix=drone_pose is not None)

        if drone_pose is None:
            # Обов'язково: короткочасна втрата фіксу (< MAX_FIX_LOSS_S) не перериває
            # поточний маневр — видана команда просто повторюється замість скидання
            # крену/тангажу в нуль. Коли фіксу немає вже ДОВШЕ MAX_FIX_LOSS_S (байдуже,
            # тривав перед тим поворот чи рівний політ), довіри до "останньої відомої"
            # команди вже нема — дрон вирівнюється (крен -> 0) і опускає ніс
            # (NOSE_DOWN_RECOVERY_PITCH_DEG), щоб камера мала кращий шанс знову побачити
            # відомі орієнтири. Щойно фікс повернеться, наступний виклик update() піде
            # звичайним шляхом нижче — "повернення до попереднього положення" відбувається
            # само собою, без окремого коду.
            fix_loss_s = math.inf if self._last_fix_time is None else now - self._last_fix_time
            held = self._last_command
            if held is not None and fix_loss_s < MAX_FIX_LOSS_S:
                return NavCommand(
                    held.roll_deg, held.pitch_deg, held.yaw_rate_deg_s, held.thrust,
                    f"WP {self.index + 1}/{len(self.waypoints)}: no fix, holding maneuver ({fix_loss_s:.1f}s)",
                    have_fix=False, waypoint_index=self.index,
                )
            return NavCommand(
                0.0, NOSE_DOWN_RECOVERY_PITCH_DEG, 0.0, CRUISE_THRUST,
                f"WP {self.index + 1}/{len(self.waypoints)}: no fix >= {MAX_FIX_LOSS_S:.0f}s — "
                f"leveling, nose down to reacquire",
                have_fix=False, waypoint_index=self.index,
            )
        self._last_fix_time = now

        lat, lon, alt = drone_pose
        east, north = latlon_to_local_m(lat, lon, ref_lat, ref_lon)

        self._update_heading(east, north, now)
        dt = 0.0 if self._last_update_time is None else now - self._last_update_time
        vertical_speed = 0.0
        if self._last_alt is not None and dt > 1e-3:
            vertical_speed = (alt - self._last_alt) / dt
        self._last_alt, self._last_update_time = alt, now

        target_lat, target_lon, target_alt = self.current_target
        target_east, target_north = latlon_to_local_m(target_lat, target_lon, ref_lat, ref_lon)
        d_east, d_north = target_east - east, target_north - north
        # ГОРИЗОНТАЛЬНА відстань до точки — суто інформативна (показ у статусі/лозі).
        # Автоматичного зарахування за радіусом більше немає: точка переходить на
        # наступну лише вручну, по натисканню Enter у вікні відео (див. advance()).
        dist_m = math.hypot(d_east, d_north)

        bearing_to_target = math.degrees(math.atan2(d_east, d_north)) % 360.0
        effective_heading = self._predicted_heading(now)
        heading_error: Optional[float] = None
        if effective_heading is not None:
            heading_error = _wrap180(bearing_to_target - effective_heading)

            # Bang-bang (найкоротша дуга перехоплення точки при обмеженому радіусі
            # віражу — класичний результат теорії Дубінса: тримати МАКСИМАЛЬНО
            # можливий крен, доки курс не вирівняється з пеленгом на ціль, і лише
            # тоді переходити на прямий політ). Свідома заміна плавного L1-закону
            # (a_lat = 2V²/L1·sin(η)) — L1 навмисно "розмотує" крен ще ЗАДОВГО до
            # вирівнювання курсу (щоб траєкторія була гладкою, як у пасажирського
            # літака), і саме тому давав ширші дуги, а не коротші, попри
            # врахування відстані. Тут крену немає причин "економити" — він
            # максимальний, поки не потрапив у ALIGN_THRESHOLD_DEG, і лише в
            # цьому вузькому коридорі лінійно спадає до нуля (щоб не смикати
            # крен туди-сюди рівно на порозі).
            if abs(heading_error) > ALIGN_THRESHOLD_DEG:
                roll_cmd = math.copysign(MAX_BANK_DEG, heading_error)
            else:
                roll_cmd = MAX_BANK_DEG * (heading_error / ALIGN_THRESHOLD_DEG)

            # yaw_rate — незалежний від крену канал (SET_ATTITUDE_TARGET.yaw_rate),
            # додає різкості довороту курсу понад те, що дає сам віраж на крені.
            yaw_rate_cmd = max(-MAX_YAW_RATE_DEG_S, min(MAX_YAW_RATE_DEG_S, KP_YAW_RATE * heading_error))
        else:
            roll_cmd = 0.0  # курс ще не оцінено (замало зміщення) — летимо рівно
            yaw_rate_cmd = 0.0

        # ── Контроль висоти у віражі ───────────────────────────────────────
        # Гістерезис: у режим відновлення входимо, коли просідання нижче цілі
        # перевищує TURN_ALT_FLOOR_DROP_M, виходимо — коли повернулись у межі
        # TURN_ALT_RECOVER_M. Без гістерезису режим смикався б на порозі.
        alt_drop = target_alt - alt  # >0 => літак НИЖЧЕ цілі
        if alt_drop > TURN_ALT_FLOOR_DROP_M:
            self._alt_recovering = True
        elif alt_drop < TURN_ALT_RECOVER_M:
            self._alt_recovering = False

        if self._alt_recovering:
            # Просів забагато — рятуємо висоту: обмежуємо крен (повертаємо
            # вертикальну складову підіймальної сили), максимальне кабрування,
            # повна тяга. Доворот курсу поки другорядний.
            roll_cmd = max(-TURN_RECOVERY_MAX_BANK_DEG, min(TURN_RECOVERY_MAX_BANK_DEG, roll_cmd))
            pitch_cmd = MAX_PITCH_DEG
            thrust_cmd = TURN_THRUST_MAX
        else:
            # Компенсація тягою: у віражі підіймальна сила має зрости в
            # 1/cos(крен) разів — інакше літак втрачає висоту. cos обмежено
            # знизу, щоб уникнути ділення на ~0 біля 90°.
            thrust_cmd = min(TURN_THRUST_MAX,
                             CRUISE_THRUST / max(0.20, math.cos(math.radians(roll_cmd))))

            # Форсований поворот, крок 2 (обов'язково): доки похибка курсу
            # перевищує ALIGN_THRESHOLD_DEG, тангаж НЕ рахується через
            # AltitudeHold — ставиться напряму в MAX_PITCH_DEG поверх кроку 1
            # (максимальний крен). Виняток — запобіжник HARD_TURN_MAX_ALT_ABOVE_TARGET_M:
            # якщо дрон уже вищий за ціль більш ніж на цю величину, форсований
            # підйом не вмикається (інакше при нестійкій оцінці курсу дрон
            # набирав би висоту без межі).
            if (heading_error is not None and abs(heading_error) > ALIGN_THRESHOLD_DEG
                    and (alt - target_alt) < HARD_TURN_MAX_ALT_ABOVE_TARGET_M):
                pitch_cmd = MAX_PITCH_DEG
            else:
                pitch_cmd = self.altitude_hold.update(target_alt, alt, vertical_speed, dt)
        pitch_cmd = max(-MAX_PITCH_DEG, min(MAX_PITCH_DEG, pitch_cmd))

        self._last_roll_cmd = roll_cmd  # ПІСЛЯ можливого обмеження — для прогнозу курсу (_predicted_heading)

        heading_text = f"{self.heading_deg:6.1f}" if self.heading_deg is not None else "   n/a"
        pred_text = f"{effective_heading:6.1f}" if effective_heading is not None else "   n/a"
        speed_text = f"{self.ground_speed_mps:4.1f}" if self.ground_speed_mps is not None else " n/a"
        alt_flag = " ALT-REC" if self._alt_recovering else ""
        status = (f"WP {self.index + 1}/{len(self.waypoints)}  dist={dist_m:6.1f}m  "
                  f"alt={alt:5.0f}/{target_alt:.0f}  "
                  f"hdg={heading_text}  pred_hdg={pred_text}  brg={bearing_to_target:6.1f}  V={speed_text}m/s  "
                  f"roll={roll_cmd:+5.1f}  pitch={pitch_cmd:+5.1f}  yaw_rate={yaw_rate_cmd:+5.1f}  "
                  f"thr={thrust_cmd:.2f}{alt_flag}")
        cmd = NavCommand(
            roll_cmd, pitch_cmd, yaw_rate_cmd, thrust_cmd, status, have_fix=True,
            waypoint_index=self.index, target_lat=target_lat, target_lon=target_lon, target_alt=target_alt,
            dist_m=dist_m, heading_deg=self.heading_deg, bearing_deg=bearing_to_target,
        )
        self._last_command = cmd
        return cmd


# ── Карта (нативне Tkinter-вікно, tkintermapview) ────────────────────────────

# Кольори маркера точки маршруту залежно від статусу: (заливка кола, обвід).
_WAYPOINT_COLORS = {
    "pending": ("#9e9e9e", "#616161"),
    "current": ("#e53935", "#b71c1c"),
    "reached": ("#43a047", "#2e7d32"),
}
_DRONE_COLOR = ("#1e88e5", "#0d47a1")   # синя лінія — наша PnP-оцінка позиції
_REAL_PATH_COLOR = "#00c853"            # зелена лінія — реальна (симуляційна) позиція,
                                         # GeoPositionDroneComponent, лише для звірки на око


class MapView:
    """
    Нативне Tkinter-вікно (tkintermapview) з треком на карті OpenStreetMap,
    без API-ключа — той самий віджет, що й у
    Tools/TestingPlatform/attitude_control/marker/map_object_marker.py, замість
    попереднього підходу з локальним HTTP-сервером + браузером (виявився
    ненадійним).

    Мітки точок маршруту — три різні кольори (сірий/очікує, червоний/поточна
    ціль, зелений/досягнуто). Синя лінія — пройдений шлях дрона за нашою
    PnP-оцінкою (ті самі згладжені фікси PoseSmoother, що й керування); синя
    мітка — поточна PnP-позиція. Зелена лінія — РЕАЛЬНА позиція дрона з сенсора
    "drone_geo_position" (GeoPositionDroneComponent), для візуальної звірки
    оцінки з істиною; керування нею не користується.

    Tkinter не потокобезпечний для довільних викликів із чужого потоку, тож усе
    вікно (root, map_widget, маркери) живе й будується ЛИШЕ у власному
    фоновому потоці (self._thread). Головний цикл відеообробки викликає лише
    update(), яка кладе новий стан у чергу (queue.Queue) — сам віджет вона
    ніколи не торкається напряму.
    """

    POLL_MS = 300

    def __init__(self, waypoints: list[tuple[float, float, float]]) -> None:
        if TkinterMapView is None:
            raise RuntimeError("tkintermapview не встановлено: pip install tkintermapview")

        self._waypoints = waypoints
        QueueItem = tuple[int, Optional[tuple[float, float, float]], Optional[tuple[float, float, float]]]
        self._queue: "queue.Queue[QueueItem]" = queue.Queue(maxsize=1)
        self._running = True
        self._ready = threading.Event()

        self._wp_markers: list = []
        self._wp_status: list[str] = ["pending"] * len(waypoints)
        self._path_points: list[tuple[float, float]] = []
        self._path_line = None
        self._drone_marker = None
        self._real_path_points: list[tuple[float, float]] = []
        self._real_path_line = None

        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=10.0)

    # ── Фоновий потік вікна — усе, що торкається Tkinter, лише тут ──────────

    def _run(self) -> None:
        self.root = tk.Tk()
        self.root.title("UAV Route Map")
        self.root.geometry("900x700")
        # Закриття хрестиком не має валити основний цикл — просто ігноруємо клік,
        # вікно завершиться разом зі скриптом (daemon-потік).
        self.root.protocol("WM_DELETE_WINDOW", lambda: None)

        self.map_widget = TkinterMapView(self.root, corner_radius=0)
        # Тайли з англійськими підписами вулиць/адрес (див. MAP_TILE_SERVER_EN).
        self.map_widget.set_tile_server(MAP_TILE_SERVER_EN, max_zoom=MAP_TILE_MAX_ZOOM)
        self.map_widget.pack(fill="both", expand=True)

        # Легенда (усі підписи англійською) — плаває поверх карти у лівому
        # верхньому куті. Пояснює обидві лінії треку та кольори міток маршруту.
        legend = tk.Label(
            self.root,
            text=("Blue line — PnP estimate (from camera)\n"
                  "Green line — real position (simulator)\n"
                  "WP markers — grey: pending  |  red: current target  |  green: reached"),
            justify="left", font=("Segoe UI", 9),
            bg="#ffffff", fg="#000000", bd=1, relief="solid", padx=6, pady=4,
        )
        legend.place(x=10, y=10)

        for i, (lat, lon, _alt) in enumerate(self._waypoints):
            self._wp_markers.append(self._make_waypoint_marker(i, lat, lon, "pending"))

        if len(self._waypoints) >= 2:
            # fit_bounding_box вимагає СПРАВЖНІй прямокутник (top-left != bottom-right) —
            # з єдиною точкою маршруту (як зараз у WAYPOINTS) top-left == bottom-right,
            # і виклик падає з ValueError; тому цей шлях лише для 2+ точок.
            lats = [wp[0] for wp in self._waypoints]
            lons = [wp[1] for wp in self._waypoints]
            self.map_widget.fit_bounding_box((max(lats), min(lons)), (min(lats), max(lons)))
        elif self._waypoints:
            self.map_widget.set_position(self._waypoints[0][0], self._waypoints[0][1])
            self.map_widget.set_zoom(MAP_ZOOM)
        else:
            self.map_widget.set_zoom(MAP_ZOOM)

        self.root.after(self.POLL_MS, self._poll)
        self._ready.set()
        self.root.mainloop()

    def _make_waypoint_marker(self, index: int, lat: float, lon: float, status: str):
        circle, outside = _WAYPOINT_COLORS[status]
        return self.map_widget.set_marker(
            lat, lon, text=f"WP {index + 1}",
            marker_color_circle=circle, marker_color_outside=outside,
        )

    def _poll(self) -> None:
        try:
            while True:
                current_index, drone_pose, real_pose = self._queue.get_nowait()
                self._apply(current_index, drone_pose, real_pose)
        except queue.Empty:
            pass
        if self._running:
            self.root.after(self.POLL_MS, self._poll)

    @staticmethod
    def _append_if_moved(points: list[tuple[float, float]], lat: float, lon: float) -> None:
        if not points:
            points.append((lat, lon))
            return
        east, north = latlon_to_local_m(lat, lon, *points[-1])
        if math.hypot(east, north) >= MIN_PATH_POINT_SPACING_M:
            points.append((lat, lon))

    def _apply(
        self,
        current_index: int,
        drone_pose: Optional[tuple[float, float, float]],
        real_pose: Optional[tuple[float, float, float]],
    ) -> None:
        # tkintermapview не має "змінити колір маркера" — найпростіше перестворити
        # маркер, якщо його статус справді змінився (рідкісна подія, не щотік).
        for i, (lat, lon, _alt) in enumerate(self._waypoints):
            status = "reached" if i < current_index else ("current" if i == current_index else "pending")
            if status != self._wp_status[i]:
                self._wp_status[i] = status
                self._wp_markers[i].delete()
                self._wp_markers[i] = self._make_waypoint_marker(i, lat, lon, status)

        if drone_pose is not None:
            lat, lon, _alt = drone_pose
            self._append_if_moved(self._path_points, lat, lon)

            if len(self._path_points) >= 2:
                if self._path_line is None:
                    self._path_line = self.map_widget.set_path(self._path_points, color=_DRONE_COLOR[0], width=3)
                else:
                    self._path_line.set_position_list(self._path_points)

            if self._drone_marker is None:
                self._drone_marker = self.map_widget.set_marker(
                    lat, lon, text="Drone (PnP)",
                    marker_color_circle=_DRONE_COLOR[0], marker_color_outside=_DRONE_COLOR[1],
                )
            else:
                self._drone_marker.set_position(lat, lon)

        if real_pose is not None:
            lat, lon, _alt = real_pose
            self._append_if_moved(self._real_path_points, lat, lon)

            if len(self._real_path_points) >= 2:
                if self._real_path_line is None:
                    self._real_path_line = self.map_widget.set_path(
                        self._real_path_points, color=_REAL_PATH_COLOR, width=3
                    )
                else:
                    self._real_path_line.set_position_list(self._real_path_points)

    # ── Публічний API — викликається з головного циклу (іншого потоку) ─────

    def update(
        self,
        current_index: int,
        drone_pose: Optional[tuple[float, float, float]],
        real_pose: Optional[tuple[float, float, float]] = None,
    ) -> None:
        """Неблокуюче: кладе найсвіжіший стан у чергу (розмір 1 — старий стан,
        який вікно не встигло забрати, просто заміняється новим)."""
        item = (current_index, drone_pose, real_pose)
        try:
            self._queue.put_nowait(item)
        except queue.Full:
            try:
                self._queue.get_nowait()
            except queue.Empty:
                pass
            try:
                self._queue.put_nowait(item)
            except queue.Full:
                pass

    def close(self) -> None:
        self._running = False
        try:
            self.root.after(0, self.root.destroy)
        except Exception:
            pass
        self._thread.join(timeout=2.0)


# ── ZMQ приймання кадрів + сенсорів ─────────────────────────────────────────

@dataclass
class Attitude:
    """
    Орієнтація корпусу від AttitudeIndicatorComponent (топік "attitude_indicator",
    payload {"roll_deg","pitch_deg","yaw_deg","roll_rate_dps","pitch_rate_dps",
    "yaw_rate_dps"}): кути крену/тангажа/рискання у градусах + відповідні кутові
    швидкості корпусу у град/с (нулі, якщо фізика в симуляторі не симулюється).
    Це РЕАЛЬНА (симуляційна) орієнтація дрона — на відміну від roll/pitch у
    NavCommand, які є КОМАНДОЮ, що ми віддаємо.
    """
    roll_deg: float
    pitch_deg: float
    yaw_deg: float
    roll_rate_dps: float
    pitch_rate_dps: float
    yaw_rate_dps: float


def parse_attitude(payload: bytes) -> Optional[Attitude]:
    """Розбирає payload топіка "attitude_indicator". None — якщо формат несподіваний."""
    try:
        data = json.loads(payload.decode("utf-8"))
        return Attitude(
            float(data["roll_deg"]), float(data["pitch_deg"]), float(data["yaw_deg"]),
            float(data.get("roll_rate_dps", 0.0)),
            float(data.get("pitch_rate_dps", 0.0)),
            float(data.get("yaw_rate_dps", 0.0)),
        )
    except (json.JSONDecodeError, UnicodeDecodeError, KeyError, TypeError, ValueError):
        return None


EnvelopeResult = tuple[
    Optional[np.ndarray], Optional[list[Landmark]],
    Optional[tuple[float, float, float]], Optional[Attitude],
]


def process_envelope(parts: list[bytes]) -> EnvelopeResult:
    """Розбирає мультипарт-повідомлення шини. Повертає (кадр камери, орієнтири, реальна
    geo-позиція дрона, орієнтація корпусу) — кожне або None, якщо відповідний топік не
    прийшов у цьому повідомленні."""
    try:
        envelope = json.loads(parts[0].decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None, None, None, None

    frame: Optional[np.ndarray] = None
    landmarks: Optional[list[Landmark]] = None
    real_pose: Optional[tuple[float, float, float]] = None
    attitude: Optional[Attitude] = None

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
            # GeoPositionDroneComponent: {"latitude","longitude","altitude_m"} — реальна
            # (симуляційна) позиція дрона, лише для порівняння з нашою PnP-оцінкою на карті.
            try:
                data = json.loads(payload.decode("utf-8"))
                real_pose = (data["latitude"], data["longitude"], data["altitude_m"])
            except (json.JSONDecodeError, UnicodeDecodeError, KeyError):
                pass
        elif topic == "attitude_indicator":
            # AttitudeIndicatorComponent: реальна орієнтація корпусу (крен/тангаж/рискання)
            # + кутові швидкості. Виводиться в інформаційному блоці на відео.
            parsed = parse_attitude(payload)
            if parsed is not None:
                attitude = parsed

    return frame, landmarks, real_pose, attitude


# ── Візуалізація одного кадру ───────────────────────────────────────────────

def draw_and_report(
    frame: np.ndarray,
    matches: list[Match],
    landmarks_visible: int,
    drone_pose: Optional[tuple[float, float, float]],
    nav_status: Optional[str],
    class_names: dict[int, str],
    video_fps: float,
    yolo_latency_ms: Optional[float],
    attitude: Optional["Attitude"] = None,
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

    # ── Позиція дрона (PnP по зіставлених орієнтирах) ────────────────────────
    if drone_pose is not None:
        lat, lon, alt = drone_pose
        drone_text = f"DRONE: lat={lat:.6f} lon={lon:.6f} alt={alt:.1f} m  (PnP, {n_matched} pts)"
        drone_color = (0, 255, 0)
        print(f"[DRONE] lat={lat:.6f}  lon={lon:.6f}  alt={alt:.1f}  (PnP, {n_matched} орієнтирів)")
    else:
        drone_text = f"DRONE: n/a (need >= {MIN_PNP_POINTS} matched landmarks, have {n_matched})"
        drone_color = (0, 0, 255)
    cv2.putText(vis, drone_text, (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.55, drone_color, 2, cv2.LINE_AA)

    info_y = 44
    if nav_status is not None:
        cv2.putText(vis, f"NAV: {nav_status}", (8, info_y), cv2.FONT_HERSHEY_SIMPLEX, 0.45,
                    (255, 200, 0), 1, cv2.LINE_AA)
        info_y += 20

    # ── Орієнтація корпусу (AttitudeIndicatorComponent, топік "attitude_indicator") ──
    if attitude is not None:
        att_text = (f"ATT: roll={attitude.roll_deg:+6.1f}  pitch={attitude.pitch_deg:+6.1f}  "
                    f"yaw={attitude.yaw_deg:+6.1f} deg   "
                    f"rates=({attitude.roll_rate_dps:+.0f}, {attitude.pitch_rate_dps:+.0f}, "
                    f"{attitude.yaw_rate_dps:+.0f}) dps")
        cv2.putText(vis, att_text, (8, info_y), cv2.FONT_HERSHEY_SIMPLEX, 0.45,
                    (0, 255, 255), 1, cv2.LINE_AA)
        info_y += 20
        print(f"[ATT] roll={attitude.roll_deg:+.1f}  pitch={attitude.pitch_deg:+.1f}  "
              f"yaw={attitude.yaw_deg:+.1f} deg   rates=({attitude.roll_rate_dps:+.1f}, "
              f"{attitude.pitch_rate_dps:+.1f}, {attitude.yaw_rate_dps:+.1f}) dps")

    status = f"landmarks visible: {landmarks_visible}"
    cv2.putText(vis, status, (8, IMG_H - 26), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)

    if yolo_latency_ms is not None:
        speed = f"video: {video_fps:5.1f} fps   yolo: {yolo_latency_ms:6.1f} ms ({1000.0 / yolo_latency_ms:4.1f} fps)"
    else:
        speed = f"video: {video_fps:5.1f} fps   yolo: waiting for first result..."
    cv2.putText(vis, speed, (8, IMG_H - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)

    return vis


# ── Глобальне відловлювання клавіш (працює і коли вікно НЕ у фокусі) ────────

# Віртуальні коди клавіш WinAPI (той самий набір, що й VK_* у winuser.h).
VK_RETURN = 0x0D
VK_Q = 0x51


class GlobalKeyWatcher:
    """
    Відловлює натискання клавіш ГЛОБАЛЬНО — навіть коли вікно відео не активне
    (не у фокусі). cv2.waitKey бачить клавіші лише поки активне саме вікно
    OpenCV; тут замість цього опитується стан клавіатури через WinAPI
    GetAsyncKeyState (user32), тож Enter спрацьовує незалежно від того, яке
    вікно зараз на передньому плані.

    Тільки Windows. Якщо user32 недоступний (інша ОС, урізаний рантайм) —
    watcher тихо вимикається (`enabled == False`, `pressed()` завжди повертає
    False), і лишається звичайна обробка через cv2.waitKey.

    `pressed(vk)` повертає True рівно ОДИН раз на кожне фізичне натискання
    (детект переднього фронту): поки клавішу тримають — далі False, аж доки її
    не відпустять і не натиснуть знову. Викликати треба РІВНО ОДИН раз за
    ітерацію циклу для кожної відстежуваної клавіші, щоб стан фронту лишався
    коректним.

    Увага: клавіша ловиться в межах усієї системи, тож натискання Enter у
    будь-якому іншому застосунку теж зарахує поточну точку маршруту — це
    зворотний бік вимоги "реагувати без фокуса".
    """

    def __init__(self, vk_codes) -> None:
        self._was_down = {vk: False for vk in vk_codes}
        self._user32 = None
        try:
            self._user32 = ctypes.windll.user32  # type: ignore[attr-defined]
        except (AttributeError, OSError):
            self._user32 = None

    @property
    def enabled(self) -> bool:
        return self._user32 is not None

    def pressed(self, vk: int) -> bool:
        if self._user32 is None:
            return False
        # Старший біт результату (0x8000) = клавіша натиснута ЗАРАЗ.
        down = bool(self._user32.GetAsyncKeyState(vk) & 0x8000)
        was_down = self._was_down.get(vk, False)
        self._was_down[vk] = down
        return down and not was_down


# ── Точка входу ──────────────────────────────────────────────────────────────

def main() -> None:
    print(f"Завантаження моделі: {WEIGHTS_PATH}")
    model = YOLO(str(WEIGHTS_PATH))
    class_names: dict[int, str] = model.names
    pipeline = DetectionPipeline(model)

    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.setsockopt(zmq.RCVHWM, 2)
    socket.connect(ZMQ_ENDPOINT)
    socket.setsockopt_string(zmq.SUBSCRIBE, "")

    cmd_socket = context.socket(zmq.PUSH)
    cmd_socket.setsockopt(zmq.SNDHWM, 1)
    cmd_socket.setsockopt(zmq.LINGER, 0)
    cmd_socket.connect(CMD_ENDPOINT)

    navigator = WaypointNavigator(WAYPOINTS)
    pose_smoother = PoseSmoother()
    frame_log_writer = None
    frame_log_file = None
    if WAYPOINTS:
        print(f"Керування увімкнено: {len(WAYPOINTS)} точок маршруту, команди -> {CMD_ENDPOINT}")
    else:
        print("WAYPOINTS порожній — керування вимкнено, скрипт лише спостерігає.")
    t_start = time.perf_counter()

    # ── Єдиний лог: один рядок на КОЖЕН оброблений кадр ────────────────────
    # (незалежно від того, чи ввімкнене керування). Об'єднує все: мітку часу,
    # реальну й PnP-оцінену geo-позицію, поточну точку маршруту та відстань до
    # неї, кути крену/тангажа/рискання й кутові швидкості (AttitudeIndicatorComponent),
    # шляхову швидкість, висоту, курс/пеленг, кількість об'єктів у кадрі / з них
    # ідентифікованих (зіставлених з орієнтиром) і всі керуючі команди.
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    frame_log_path = LOG_DIR / f"flight_log_{time.strftime('%Y%m%d_%H%M%S')}.csv"
    frame_log_file = frame_log_path.open("w", newline="", encoding="utf-8")
    frame_log_writer = csv.DictWriter(frame_log_file, fieldnames=[
        "wall_time", "t_s",
        "waypoint_index", "target_lat", "target_lon", "target_alt_m", "dist_to_target_m",
        "real_lat", "real_lon", "real_alt_m",
        "pnp_lat", "pnp_lon", "pnp_alt_m",
        "roll_deg", "pitch_deg", "yaw_deg",
        "roll_rate_dps", "pitch_rate_dps", "yaw_rate_dps",
        "ground_speed_mps", "altitude_m",
        "heading_deg", "bearing_deg",
        "objects_in_frame", "objects_identified",
        "cmd_roll_deg", "cmd_pitch_deg", "cmd_yaw_rate_dps", "cmd_thrust",
        "have_fix", "nav_status",
    ])
    frame_log_writer.writeheader()
    frame_log_file.flush()
    print(f"Лог (на кожен кадр): {frame_log_path}")

    map_view = MapView(WAYPOINTS) if WAYPOINTS else None
    if map_view is not None:
        print("Вікно карти відкрито (окреме Tkinter-вікно).")

    latest_landmarks: list[Landmark] = []
    # Опорна точка для локальної ENU-площини (як ref_lat/ref_lon у test-скрипті) —
    # фіксується один раз, по першому отриманому списку орієнтирів, і більше не
    # змінюється, щоб PnP-оцінки різних кадрів лишались у одній системі координат.
    ref_lat: Optional[float] = None
    ref_lon: Optional[float] = None
    # Реальна (симуляційна) позиція дрона з GeoPositionDroneComponent — лише для
    # порівняння з нашою PnP-оцінкою на карті (зелена лінія), керування нею не користується.
    latest_real_pose: Optional[tuple[float, float, float]] = None
    # Реальна орієнтація корпусу з AttitudeIndicatorComponent (топік "attitude_indicator") —
    # виводиться в інформаційному блоці на відео.
    latest_attitude: Optional[Attitude] = None

    video_fps = 0.0
    last_frame_time = time.perf_counter()

    # Enter/'q' ловляться глобально (працює й коли вікно відео не у фокусі);
    # cv2.waitKey лишається запасним шляхом, коли вікно активне або коли
    # GetAsyncKeyState недоступний (не-Windows).
    key_watcher = GlobalKeyWatcher((VK_RETURN, VK_Q))
    focus_note = "" if key_watcher.enabled else " (лише коли вікно відео у фокусі)"

    print(f"Підключено до {ZMQ_ENDPOINT}. Очікування кадрів з камери... "
          f"(Enter — наступна точка маршруту, 'q' — вихід{focus_note})")

    try:
        while True:
            if not socket.poll(timeout=1000):
                continue

            parts = socket.recv_multipart()
            if len(parts) < 2:
                continue

            frame, landmarks, real_pose, attitude = process_envelope(parts)
            if landmarks is not None:
                latest_landmarks = landmarks
                if ref_lat is None and landmarks:
                    ref_lat = sum(lm.latitude for lm in landmarks) / len(landmarks)
                    ref_lon = sum(lm.longitude for lm in landmarks) / len(landmarks)
                    print(f"Опорна точка ENU зафіксована: lat={ref_lat:.6f} lon={ref_lon:.6f}")
            if real_pose is not None:
                latest_real_pose = real_pose
            if attitude is not None:
                latest_attitude = attitude

            if frame is None:
                continue

            now = time.perf_counter()
            dt = now - last_frame_time
            last_frame_time = now
            if dt > 0:
                instant_fps = 1.0 / dt
                video_fps = instant_fps if video_fps == 0.0 else video_fps * 0.9 + instant_fps * 0.1

            matches = pipeline.tick(frame, latest_landmarks)

            drone_pose = None
            if ref_lat is not None:
                raw_pose = estimate_drone_position(matches, ref_lat, ref_lon)
                if raw_pose is not None:
                    drone_pose = pose_smoother.update(raw_pose, now)

            # Керування веде дрон по РЕАЛЬНІй позиції з drone_geo_position
            # (GeoPositionDroneComponent), а не по PnP-оцінці з зображення.
            # drone_pose (PnP) лишається лише для синьої лінії на карті — звірка.
            nav_pose = latest_real_pose

            nav_cmd: Optional[NavCommand] = None
            if WAYPOINTS:
                if ref_lat is not None:
                    nav_cmd = navigator.update(nav_pose, ref_lat, ref_lon)
                else:
                    nav_cmd = NavCommand(
                        0.0, 0.0, 0.0, CRUISE_THRUST,
                        "waiting for reference point (no landmarks seen yet)", have_fix=False,
                    )
                try:
                    cmd_socket.send_json(
                        build_command(nav_cmd.roll_deg, nav_cmd.pitch_deg,
                                      nav_cmd.yaw_rate_deg_s, nav_cmd.thrust),
                        flags=zmq.NOBLOCK,
                    )
                except zmq.Again:
                    pass

                print(f"[CMD] roll={nav_cmd.roll_deg:+6.1f} pitch={nav_cmd.pitch_deg:+6.1f} "
                      f"yaw_rate={nav_cmd.yaw_rate_deg_s:+5.1f} thrust={nav_cmd.thrust:.2f}  "
                      f"| {nav_cmd.status}")

                if map_view is not None:
                    map_view.update(navigator.index, drone_pose, latest_real_pose)

            # ── Єдиний лог: усе про цей кадр в один рядок CSV ──────────────
            if frame_log_writer is not None:
                objs_in_frame = len(matches)
                objs_identified = sum(1 for m in matches if m[5] is not None)
                r_lat, r_lon, r_alt = (latest_real_pose if latest_real_pose is not None
                                       else (None, None, None))
                p_lat, p_lon, p_alt = drone_pose if drone_pose is not None else (None, None, None)
                att = latest_attitude
                frame_log_writer.writerow({
                    "wall_time": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "t_s": f"{now - t_start:.3f}",
                    "waypoint_index": "" if nav_cmd is None else _opt(nav_cmd.waypoint_index, "{}"),
                    "target_lat": _opt(nav_cmd.target_lat if nav_cmd is not None else None),
                    "target_lon": _opt(nav_cmd.target_lon if nav_cmd is not None else None),
                    "target_alt_m": _opt(nav_cmd.target_alt if nav_cmd is not None else None, "{:.2f}"),
                    "dist_to_target_m": _opt(nav_cmd.dist_m if nav_cmd is not None else None, "{:.2f}"),
                    "real_lat": _opt(r_lat), "real_lon": _opt(r_lon), "real_alt_m": _opt(r_alt, "{:.2f}"),
                    "pnp_lat": _opt(p_lat), "pnp_lon": _opt(p_lon), "pnp_alt_m": _opt(p_alt, "{:.2f}"),
                    "roll_deg": _opt(att.roll_deg if att else None, "{:.2f}"),
                    "pitch_deg": _opt(att.pitch_deg if att else None, "{:.2f}"),
                    "yaw_deg": _opt(att.yaw_deg if att else None, "{:.2f}"),
                    "roll_rate_dps": _opt(att.roll_rate_dps if att else None, "{:.2f}"),
                    "pitch_rate_dps": _opt(att.pitch_rate_dps if att else None, "{:.2f}"),
                    "yaw_rate_dps": _opt(att.yaw_rate_dps if att else None, "{:.2f}"),
                    "ground_speed_mps": _opt(navigator.ground_speed_mps, "{:.2f}"),
                    "altitude_m": _opt(r_alt, "{:.2f}"),
                    "heading_deg": _opt(nav_cmd.heading_deg if nav_cmd is not None else None, "{:.2f}"),
                    "bearing_deg": _opt(nav_cmd.bearing_deg if nav_cmd is not None else None, "{:.2f}"),
                    "objects_in_frame": objs_in_frame,
                    "objects_identified": objs_identified,
                    "cmd_roll_deg": _opt(nav_cmd.roll_deg if nav_cmd is not None else None, "{:.3f}"),
                    "cmd_pitch_deg": _opt(nav_cmd.pitch_deg if nav_cmd is not None else None, "{:.3f}"),
                    "cmd_yaw_rate_dps": _opt(nav_cmd.yaw_rate_deg_s if nav_cmd is not None else None, "{:.3f}"),
                    "cmd_thrust": _opt(nav_cmd.thrust if nav_cmd is not None else None, "{:.3f}"),
                    "have_fix": "" if nav_cmd is None else int(nav_cmd.have_fix),
                    "nav_status": nav_cmd.status if nav_cmd is not None else "",
                })
                frame_log_file.flush()

            vis = draw_and_report(
                frame, matches, len(latest_landmarks), drone_pose,
                nav_cmd.status if nav_cmd is not None else None,
                class_names, video_fps, pipeline.last_latency_ms,
                attitude=latest_attitude,
            )

            cv2.imshow("UAV object geolocation", vis)
            key = cv2.waitKey(1) & 0xFF
            # pressed() опитуємо РІВНО раз за ітерацію для кожної клавіші, щоб
            # детект переднього фронту лишався коректним (тому — окремі змінні,
            # а не короткозамкнене `or`).
            enter_global = key_watcher.pressed(VK_RETURN)
            quit_global = key_watcher.pressed(VK_Q)
            if key == ord("q") or quit_global:
                break
            if (key in (13, 10) or enter_global) and WAYPOINTS:  # Enter — зарахувати поточну точку вручну
                navigator.advance()

    except KeyboardInterrupt:
        print("Зупинено користувачем.")
    finally:
        if WAYPOINTS:
            # Плавний вихід у нейтраль — той самий прийом, що й у circle_autopilot.py.
            for _ in range(5):
                try:
                    cmd_socket.send_json(build_command(0.0, 0.0, 0.0, CRUISE_THRUST), flags=zmq.NOBLOCK)
                except zmq.Again:
                    pass
                time.sleep(0.02)
        pipeline.close()
        cmd_socket.close(0)
        socket.close()
        context.term()
        cv2.destroyAllWindows()
        if frame_log_file is not None:
            frame_log_file.close()
        if map_view is not None:
            map_view.close()


if __name__ == "__main__":
    main()
