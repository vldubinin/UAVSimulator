"""
Визначення глобальних координат об'єктів + власної позиції дрона у реальному
часі з відеопотоку симулятора БПЛА (UAVSimulator), який роздається через ZMQ
(SensorBusComponent, tcp://*:5555 — PUB, мультипарт-повідомлення: part 0 =
JSON-конверт {"sensors": [{"topic": ...}, ...]}, part 1..N = сирі payload'и
сенсорів у тому самому порядку).

Отримання даних (ZMQ, YOLO, зіставлення з орієнтирами, PnP-оцінка власної
позиції) та відображення (відео + карта, в ОДНОМУ Tkinter-вікні — AppWindow,
відео зліва, карта справа) — як і раніше.

Автопілот (PathFollower + CessnaEKF) веде літак уздовж наперед розрахованої
Dubins-траєкторії (WAYPOINTS -> compute_dubins_route_profile, той самий шлях,
що намальований чорним пунктиром на карті):
  - Тангаж СВІДОМО обмежений вузьким, сильно зсунутим униз коридором
    (PITCH_MAX_UP_DEG..PITCH_MAX_DOWN_DEG) — власна позиція літака
    визначається ВИКЛЮЧНО по видимих у кадрі камери орієнтирах, тож підняти
    ніс вище PITCH_MAX_UP_DEG означає ризик відвести камеру від землі й
    втратити їх з поля зору; опускати ніс, навпаки, лише покращує огляд.
  - Курс тримається "чистим переслідуванням" (pure pursuit) точки на
    LOOKAHEAD_TIME_S секунд попереду поточного прогресу вздовж траєкторії (за
    поточною швидкістю EKF). Крен рахується НЕ напряму пропорційно похибці
    курсу, а через бажану швидкість розвороту (град/с, не залежить від
    швидкості польоту) і обернену формулу координованого віражу
    (крен=atan(turn_rate·V/g)) — від ПОТОЧНОЇ оціненої EKF-швидкості V, щоб
    той самий крен не давав вкрай різний фактичний радіус повороту залежно
    від того, наскільки реальна швидкість відрізняється від планової.
  - Між PnP-фіксами (а надто — коли їх довго нема, наприклад орієнтири
    пропали з кадру) власна позиція/курс/швидкість НЕ просто "заморожуються"
    чи екстраполюються по колишній швидкості, а оцінюються розширеним
    фільтром Калмана (CessnaEKF): predict() рухає стан уперед за спрощеною
    фізичною моделлю Cessna 172N (координований віраж від крену, набір/
    зниження від тангажу, розгін/гальмування від тяги), керованою ОСТАННЬОЮ
    виданою автопілотом командою, — тобто симулюється реакція літака на
    власні керуючі сигнали, а не сліпа екстраполяція. update() зливає це
    передбачення зі свіжим PnP-фіксом, коли він є (з воротами на викид —
    тест Махаланобіса проти невизначеності фільтра, аналог MAX_POSE_SPEED_MPS
    з попередніх версій).
  - Команди йдуть у форматі SET_ATTITUDE_TARGET (build_command) по ZMQ
    PUSH -> CMD_ENDPOINT (tcp://127.0.0.1:5556) — той самий канал/формат, що
    й Tools/TestingPlatform/attitude_control/circle_autopilot.py.

Використовує сенсори:
  - "camera"         — JPEG-кадр (CameraFrameComponent, 640x480, HFOV=90°,
                        тобто fx=fy=320px, головна точка в центрі кадру,
                        дисторсія відсутня).
  - "cesium_objects" — {"objects": [{"id","latitude","longitude","altitude",
                        "pixel_x","pixel_y","visible"}, ...]} від
                        CesiumSurroundingsScannerComponent: список орієнтирів,
                        які зараз видно з камери, з їхніми РЕАЛЬНИМИ глобальними
                        координатами.
  - "custom_objects" — той самий формат від CustomSurroundingsScannerComponent
                        (якщо використовується замість/разом із cesium_objects).
  - "drone_geo_position" — {"latitude","longitude","altitude_m"} від
                        GeoPositionDroneComponent: РЕАЛЬНА (симуляційна)
                        позиція дрона — використовується лише для показу на
                        карті зеленою лінією поряд із нашою PnP-оцінкою
                        (синя лінія).

Підхід до самого позиціювання (без змін):
  1. Детекції YOLO зіставляються з видимими орієнтирами по близькості центру
     bbox до pixel_x/pixel_y орієнтира (match_detection_to_landmark) —
     координати ОБ'ЄКТІВ беруться напряму з орієнтира, без обчислень.
  2. Ті самі зіставлення (2D-піксель <-> відома 3D geo-точка) йдуть у
     cv2.solvePnPRansac (estimate_drone_position) — звідси береться сирий
     PnP-фікс ВЛАСНОЇ позиції дрона (позиції камери), потрібно щонайменше
     MIN_PNP_POINTS зіставлень одночасно в кадрі. Цей сирий фікс іде в
     CessnaEKF.update() (див. вище) — окремого EMA-згладжування більше нема,
     цю роль тепер виконує сам фільтр Калмана.

YOLO (full-set-best.pt) не блокує відеопотік: інференс запускається у
фоновому потоці (DetectionPipeline, ThreadPoolExecutor(max_workers=1)). Усі
кадри, що надходять, поки YOLO зайнятий попереднім, просто пропускаються —
показується останній доступний результат YOLO.

Вікно (AppWindow) — нативне Tkinter-вікно, поділене ГОРИЗОНТАЛЬНО на дві
частини: згори карта (tkintermapview, рельси OpenStreetMap, без API-ключа) на
всю ширину; знизу — смуга з трьох колонок (зліва направо): ползунки
налаштування автопілоту, текстова інформаційна панель і відео (OpenCV
overlay, намальований draw_and_report, конвертований у Tkinter-зображення
через Pillow).
Мітки маршруту WAYPOINTS — три кольори за прогресом PathFollower (сіра/
очікує, червона/поточна ціль, зелена/пройдено). Синя лінія + синя мітка —
пройдений шлях і поточна позиція за оцінкою EKF (не сирий PnP і не WAYPOINTS-
навігатор); зелена лінія + зелена мітка (з висотою в підписі) — РЕАЛЬНА
(симуляційна) позиція дрона з "drone_geo_position", для візуальної звірки
оцінки з істиною; чорний пунктир — запланована Dubins-траєкторія (розрахована
один раз при старті).
Tkinter не потокобезпечний для довільних викликів з чужого потоку, тож усе
вікно живе у власному фоновому потоці; головний цикл (ZMQ + YOLO + PnP + EKF +
автопілот) лише кладе оновлення в чергу (queue.Queue).
Встановлення: `pip install tkintermapview pillow`.

Запуск: `python uav_object_geolocation_view.py` під час активної симуляції з
увімкненим SensorBusComponent (і хоча б одним із cesium_objects/custom_objects
сенсорів). 'q' у вікні (або закриття вікна) завершує роботу.
"""

from __future__ import annotations

import csv
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
from PIL import Image, ImageTk
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

DEG_M = 111_320.0  # метрів на градус широти (наближення)
MIN_PNP_POINTS = 6       # запас понад мінімальні 4 точки EPnP — щоб RANSAC міг відкинути 1-2 хибні
PNP_REPROJECTION_ERROR_PX = 8.0  # поріг inlier'а для cv2.solvePnPRansac, пікселі

CONF_THRESHOLD: float = 0.25

# Поріг зіставлення детекції з орієнтиром: частка діагоналі кадру.
MATCH_MAX_DIST_FRAC: float = 0.15
MATCH_MAX_DIST_PX: float = MATCH_MAX_DIST_FRAC * math.hypot(IMG_W, IMG_H)

# Ключові точки маршруту: (latitude, longitude, altitude_m) — лише для
# довідкового показу на карті (керування ними в цій версії не займається).
WAYPOINTS: list[tuple[float, float, float]] = [
    (50.0426466, 36.2802352, 90.0),
    (50.0472896, 36.2835316, 90.0),
    (50.0500375, 36.2940030, 90),
    (50.0419975, 36.2908175, 90)
]

MAP_ZOOM: int = 15
MIN_PATH_POINT_SPACING_M: float = 1.0  # не додавати нову точку треку, поки дрон не відійшов на стільки

# ── Планування маршруту через Dubins paths (Cessna 172N) ────────────────────

# Летові параметри Cessna 172N, використані лише для розрахунку радіуса
# віражу (координований віраж: R = V² / (g·tan(крен))):
CESSNA172N_CRUISE_SPEED_MPS: float = 62.0  # ~120 KTAS, крейсер на ~75% потужності (POH Cessna 172N)
CESSNA172N_BANK_DEG: float = 30.0          # комфортний/типовий для планування маршруту віраж
GRAVITY_MPS2: float = 9.81

DUBINS_TURN_RADIUS_M: float = (
    CESSNA172N_CRUISE_SPEED_MPS ** 2 / (GRAVITY_MPS2 * math.tan(math.radians(CESSNA172N_BANK_DEG)))
)

DUBINS_SAMPLE_STEP_M: float = 5.0   # крок дискретизації дуг/прямих ділянок траєкторії для показу на карті
DUBINS_DASH_ON_M: float = 14.0      # довжина "риски" пунктиру, метри
DUBINS_DASH_OFF_M: float = 10.0     # довжина проміжку пунктиру, метри
_PLANNED_ROUTE_COLOR: str = "#000000"  # чорний пунктир — запланована Dubins-траєкторія

# ── Автопілот: слідування за розрахованою Dubins-траєкторією ────────────────

CMD_ENDPOINT: str = "tcp://127.0.0.1:5556"  # ZMQ PUSH -> PULL у UAttitudeControlComponent

# Компенсування похибки курсу — bang-bang закон, скопійований (мінімальні
# зміни) з WaypointNavigator у uav_object_geolocationо.py: найкоротша дуга
# перехоплення курсу при обмеженому радіусі віражу (теорія Дубінса — за
# відсутності вимог до кінцевого курсу оптимально тримати МАКСИМАЛЬНИЙ крен,
# доки курс не збігся з ціллю, і лише тоді переходити на прямий політ).
# Крен максимальний (±AUTOPILOT_MAX_BANK_DEG), поки похибка курсу не потрапить
# у ALIGN_THRESHOLD_DEG, і лише в цьому вузькому коридорі спадає до нуля
# лінійно (щоб не смикати крен туди-сюди рівно на порозі). yaw_rate —
# додатковий, незалежний від крену канал доворту курсу поверх самого віражу.
AUTOPILOT_MAX_BANK_DEG: float = 75.0   # ЖОРСТКА межа крену (і водночас — сам bang-bang крен, доки не
                                        # вирівнялись за ALIGN_THRESHOLD_DEG)
ALIGN_THRESHOLD_DEG: float = 10.0      # у межах цього коридору крен лінійно спадає до нуля замість bang-bang
MAX_YAW_RATE_DEG_S: float = 30.0       # додатковий, незалежний від крену канал довороту курсу
KP_YAW_RATE: float = 1.0

# Множник (0..1) на бажаний крен залежно від dist_to_nearest_m (crosstrack-
# відхилення від траєкторії) — чим ближче літак до лінії шляху, тим менший
# крен: щойно курс майже вирівняно з траєкторією, різкий віраж лише розгойдує
# літак навколо неї (перерегулювання), а не покращує точність. Кусково-лінійна
# крива (не єдиний коефіцієнт) — редагується мишею у вікні (AppWindow._CurveEditor,
# точки лишаються на фіксованому X, драгабельний лише Y) і зберігається разом з
# рештою ползунків у TUNING_SETTINGS_PATH. _eval_roll_distance_curve — обчислення
# значення кривої в довільній точці; за межами діапазону контрольних точок —
# крайнє значення, без екстраполяції.
ROLL_DISTANCE_CURVE_MAX_M: float = 200.0  # правий край кривої (X) — далі множник фіксований на значенні крайньої точки
DEFAULT_ROLL_DISTANCE_CURVE: list[tuple[float, float]] = [
    (0.0, 0.15), (50.0, 0.4), (100.0, 0.7), (150.0, 1.0), (200.0, 1.0),
]

# Крива вище реагує на СИРУ crosstrack-дистанцію — тобто вирівнювання
# починається лише коли літак ВЖЕ близько до траєкторії. При високій
# швидкості наближення (велика проєкція швидкості на напрямок до цілі) цього
# запізно: за час, поки крен фактично спаде (roll_rate_limit_deg_s), літак
# встигає проскочити далі, ніж на повільній швидкості — тобто мало б
# вирівнюватись РАНІШЕ (за більшої дистанції). alignment_lead_time_s —
# шостий числовий ползунок AppWindow: наскільки секунд наближення "наперед"
# зазирати. Ефективна дистанція, яка йде в криву замість сирої:
#   effective = max(0, dist_to_nearest_m - closing_speed_mps * alignment_lead_time_s)
# де closing_speed_mps — складова швидкості САМЕ в напрямку цілі (>= 0,
# від'ємну — тобто рух ВІД цілі — ігноруємо, там ранішого вирівнювання не
# треба). При lead_time=0 поведінка ідентична попередній (без випередження).
DEFAULT_ALIGNMENT_LEAD_TIME_S: float = 2.0

# Тангаж СВІДОМО обмежений вузьким, сильно зсунутим униз коридором: власна
# позиція літака визначається ВИКЛЮЧНО по видимих у кадрі камери орієнтирах
# (той самий PnP-конвеєр, що й раніше), тож підняти ніс вище PITCH_MAX_UP_DEG
# означає ризик відвести камеру від землі й втратити орієнтири з поля зору;
# опускати ніс, навпаки, лише покращує огляд.
PITCH_MAX_UP_DEG: float = 4.0
PITCH_MAX_DOWN_DEG: float = -12.0

# Лог показав: реальна крейсерська швидкість у симуляторі (~20-25 м/с, за
# EKF-оцінкою, узгодженою з 97%+ прийнятих PnP-вимірів) значно нижча за
# заплановану CESSNA172N_CRUISE_SPEED_MPS=62 м/с, попри тягу лише 0.45-0.80
# (є запас до максимуму 1.0). Підняті нижче значення — спроба фактично
# розігнати літак ближче до швидкості, під яку розрахований і сам маршрут
# (DUBINS_TURN_RADIUS_M), і lookahead/крен (тепер уже адаптивні до реальної
# швидкості, але вся траєкторія пролітається довше й з меншим запасом
# маневреності на нижчій швидкості).
CRUISE_THRUST: float = 0.85
CLIMB_THRUST: float = 1.00    # максимум — коли попереду вздовж траєкторії треба набрати висоту
DESCEND_THRUST: float = 0.65  # менше тяги на зниження — тангаж і так обмежений, тож знижуємось повільно
ALT_CLIMB_THRESHOLD_M: float = 15.0
ALT_DESCEND_THRESHOLD_M: float = 15.0

# "Чисте переслідування" (pure pursuit): точка на LOOKAHEAD_TIME_S секунд
# ПОПЕРЕДУ (за поточною швидкістю EKF, не фіксована відстань) — миттєва ціль
# курсу. Час, а не метри: на низькій швидкості ціль ближче (менше "зрізає"
# повороти), на високій — далі (менше "смикається"), замість фіксованих
# 220м, розрахованих під один-єдиний нominal 62 м/с.
#
# Аналіз логу (nav_commands_20260905_183658.csv) показав крос-трек відхилення
# від запланованої траєкторії до 203м (середнє 50.6м) — і чітко гірше саме на
# низькій швидкості (59.8м при V<100км/год проти 6.1м при V>=100км/год).
# Тест підтвердив механізм: коли ОЦІНЕНА EKF-швидкість завищена відносно
# реальної (а таке зміщення в нас підтверджено раніше), lookahead-точка й
# розрахунок крену обидва орієнтуються на завищену швидкість — літак "зрізає"
# широкі заплановані повороти. Коротший lookahead зменшує абсолютну похибку
# (у метрах) від будь-якого такого неточного виміру швидкості: тест на тій
# самій траєкторії з реалістичним розходженням моделі показав падіння
# середнього відхилення з 6.7м до 1.9м (максимум з 16.1м до 7.5м) при
# зменшенні LOOKAHEAD_TIME_S з 4с до 2с і LOOKAHEAD_MAX_M з 400м до 200м.
LOOKAHEAD_TIME_S: float = 2.0
LOOKAHEAD_MIN_M: float = 60.0
LOOKAHEAD_MAX_M: float = 200.0
PATH_CORRECTIVE_WINDOW_POINTS: int = 40  # +/- вікно навколо індексу, передбаченого інтегруванням
                                          # пройденої відстані (див. PathFollower.update) —
                                          # вузьке НАВМИСНО: короткі ноги маршруту (коротші за
                                          # DUBINS_TURN_RADIUS_M) дають Dubins-петлі, що самі себе
                                          # перетинають у просторі; широке вікно пошуку найближчої
                                          # точки хибно "перестрибувало" б на інший виток тієї самої
                                          # петлі, а не туди, де літак справді перебуває вздовж шляху
ARRIVAL_RADIUS_M: float = 40.0       # місію завершено в межах цього радіуса від останньої точки шляху

# Довше цього без ПРИЙНЯТОГО PnP-фіксу (havе_measurement=False в EKF) —
# довіри до курсу, накопиченого "наосліп" через мертву точку, уже нема:
# вирівнюємось (крен/yaw_rate -> 0) і форсуємо ніс до PITCH_MAX_DOWN_DEG, щоб
# дати камері якнайкращий шанс знову побачити орієнтири.
MAX_FIX_LOSS_S: float = 5.0

# Запобіжник: якщо фіксу нема ДОВГО (наприклад, орієнтири зникли з поля зору
# й більше не з'являються), forced nose-down ("no fix" гілка вище) інакше
# триває нескінченно — EKF-оцінка висоти монотонно падає без жодної межі
# (підтверджено логом: висота пішла в мінус, до -392 м, за ~60с без фіксу,
# доки лог не закінчився). Нижче цієї висоти "no fix"-гілка перестає
# форсувати піке і натомість тримає/набирає RECOVERY_MIN_ALT_M — тобто
# перестає пікірувати, коли й так уже небезпечно низько, навіть якщо
# орієнтири так і не знайшлися.
RECOVERY_MIN_ALT_M: float = 40.0

LOG_DIR = Path(__file__).resolve().parent / "logs"  # CSV-лог команд керування

# Збережені значення п'яти ползунків AppWindow (roll_gain/pitch_gain/
# tail_threshold_deg/switch_distance_m/roll_rate_limit_deg_s) — щоб підібране
# налаштування не губилось між запусками.
TUNING_SETTINGS_PATH = Path(__file__).resolve().parent / "autopilot_tuning.json"
TUNING_SAVE_DEBOUNCE_MS = 500  # запис на диск не частіше, ніж раз на стільки мс після останнього руху ползунка

# ── EKF: оцінка позиції/курсу/швидкості Cessna 172N між PnP-фіксами ─────────
#
# Стан x = [east, north, alt, yaw, speed] (yaw — той самий математичний курс
# atan2(north,east), що й у Dubins-плануванні вище). predict() рухає стан
# уперед КОЖЕН тік (навіть без нового PnP-фіксу) за спрощеною фізичною
# моделлю Cessna 172N (_cessna_process_model), керованою ОСТАННЬОЮ виданою
# автопілотом командою — це і є "розрахунок положення з урахуванням
# керуючих сигналів" при втраті фіксу, а не гола екстраполяція.
CESSNA172N_MIN_SPEED_MPS: float = 25.0  # трохи вище швидкості звалювання (~48 KIAS)
CESSNA172N_MAX_SPEED_MPS: float = 70.0  # трохи вище крейсерської — запас керування тягою
SPEED_TIME_CONSTANT_S: float = 20.0     # стала часу розгону/гальмування під зміну тяги (спрощено) —
                                         # свідомо повільна: чим швидше ЦЯ детермінована "тяга до цілі"
                                         # від тяги діє щопередбачення, тим сильніше вона перетягує
                                         # оцінку геть від прямого виміру швидкості (update() вище) між
                                         # PnP-фіксами; довша стала часу лишає моделі менше "голосу"
                                         # проти реального виміру (підтверджено тестом: зміщення
                                         # оцінки від реальної швидкості впало приблизно вдвічі)

# _cessna_process_model — СПРОЩЕНА фізична модель (координований віраж,
# climb_rate=V*sin(pitch) тощо), яка НЕ зобов'язана точно збігатися з
# реальною фізикою UAttitudeControlComponent у симуляторі. Якщо довіра до
# неї (через занижений процесний шум нижче) виставлена зависокою, невизначе-
# ність фільтра P росте повільніше за РЕАЛЬНЕ розходження моделі з дійсністю
# — тоді щоразу свіжіший (і правильний!) PnP-фікс виглядає як "неможливий
# викид" відносно вже хибного передбачення і відхиляється воротами, похибка
# фільтра лише накопичується, і жоден наступний вимір узагалі більше не
# приймається (підтверджено логом: 181 з 181 PnP-фіксів із 20+ зіставлених
# орієнтирів кожен — відхилено фільтром 100% часу, оцінка позиції розійшлася
# з реальною траєкторією без жодної корекції). Тому нижче процесний шум
# зумисно ВЕЛИКИЙ — фільтр визнає свою модель ненадійною і схильний довіряти
# свіжому виміру, а не мертвій точці.
EKF_PROCESS_STD_POS_MPS: float = 15.0    # неврахована моделлю похибка позиції, м/с
EKF_PROCESS_STD_YAW_DEG_S: float = 20.0  # неврахована моделлю похибка курсу, °/с
EKF_PROCESS_STD_SPEED_MPS: float = 5.0   # неврахована моделлю похибка швидкості, м/с
EKF_MEASUREMENT_STD_POS_M: float = 6.0   # приблизна точність PnP-фіксу (RANSAC inliers), м
EKF_MEASUREMENT_STD_ALT_M: float = 8.0
EKF_INITIAL_STD_POS_M: float = 10.0
EKF_INITIAL_STD_YAW_DEG: float = 20.0
EKF_INITIAL_STD_SPEED_MPS: float = 5.0
EKF_OUTLIER_GATE_CHI2_3DOF: float = 11.345  # 99% довірчий інтервал χ², 3 ступені свободи —
                                             # вимір, що йому не відповідає, ігнорується (ворота на викид)

# Пряме обчислення швидкості з РЕАЛЬНОГО положення (|Δпозиція між двома
# послідовними ПРИЙНЯТИМИ сирими PnP-фіксами| / Δt), а не лише опосередкована
# оцінка через крос-кореляції P із самою моделлю розгону/гальмування. На
# надто короткій базі похибка позиції домінує над самим переміщенням —
# дисперсія цього виміру росте як 1/dt², тож замість жорсткого порогу нижче
# просто ігноруємо базу коротшу за цей мінімум (майже нульовий внесок такого
# виміру дає той самий ефект природним чином, але без зайвого шуму/переповнення).
MIN_SPEED_MEAS_DT_S: float = 0.05

# Запобіжник від зациклення "ворота відхиляють усе назавжди" (саме це й
# спостерігалося в лозі): якщо стільки вимірів ПОСПІЛЬ відхилено, це вже
# ознака того, що розійшовся САМ ФІЛЬТР (модель), а не що вимірам не можна
# довіряти — наступний вимір приймається ПРИМУСОВО (позиція/висота "стрибком"
# скидається на нього, а не змішується малим коефіцієнтом Калмана), і лічильник
# скидається.
EKF_MAX_CONSECUTIVE_REJECTIONS: int = 5


# ── Геодезичні хелпери ───────────────────────────────────────────────────────

def latlon_to_local_m(lat: float, lon: float, ref_lat: float, ref_lon: float) -> tuple[float, float]:
    east = (lon - ref_lon) * math.cos(math.radians(ref_lat)) * DEG_M
    north = (lat - ref_lat) * DEG_M
    return east, north


def local_m_to_latlon(east: float, north: float, ref_lat: float, ref_lon: float) -> tuple[float, float]:
    lat = ref_lat + north / DEG_M
    lon = ref_lon + east / (math.cos(math.radians(ref_lat)) * DEG_M)
    return lat, lon


# ── Dubins paths: планування маршруту через усі цільові точки ───────────────
#
# Класична аналітична побудова Dubins-шляху (Dubins, 1957 — див. також LaValle,
# "Planning Algorithms", розд. 15.3.1): для пари поз (x,y,курс) з фіксованим
# радіусом віражу перебираються всі 6 типів шляху (LSL, RSR, LSR, RSL, RLR,
# LRL — L/R = дуга наліво/направо, S = пряма), і обирається найкоротший.
# Локальна (east, north) ENU-площина тут використовується напряму як звичайна
# декартова (x, y): схід -> вісь x, північ -> вісь y; оскільки поворот від
# осі x (схід) до осі y (північ) на +90° — це водночас і математично "проти
# годинникової стрілки" (L у Dubins), і фізично лівий віраж (курс 090° ->
# 000° зменшується — це лівий поворот), додаткового дзеркалення координат не
# потрібно.

def _mod2pi(theta: float) -> float:
    return theta % (2.0 * math.pi)


def _dubins_path_candidates(alpha: float, beta: float, d: float) -> list[tuple[str, float, float, float]]:
    """Повертає (mode, t, p, q) для кожного з 6 типів шляху, що має розв'язок
    (t, p, q — нормовані на радіус: кути дуг у радіанах, довжина прямої в
    одиницях радіуса)."""
    sin_a, cos_a = math.sin(alpha), math.cos(alpha)
    sin_b, cos_b = math.sin(beta), math.cos(beta)
    cos_ab = math.cos(alpha - beta)
    out: list[tuple[str, float, float, float]] = []
    # Допуск на похибку округлення з рухомою комою: геометрично вироджені
    # випадки (наприклад, розв'язок лежить точно на межі, p == 0) інколи
    # дають p_sq трохи ВІД'ЄМНИМ (наприклад, -1e-16 замість 0.0) — без цього
    # допуску такий шлях хибно відкидався б як "нездійсненний", хоча
    # насправді саме він і є найкоротшим.
    EPS = 1e-9

    # LSL
    p_sq = 2.0 + d * d - 2.0 * cos_ab + 2.0 * d * (sin_a - sin_b)
    if p_sq >= -EPS:
        p = math.sqrt(max(p_sq, 0.0))
        tmp = math.atan2(cos_b - cos_a, d + sin_a - sin_b)
        t = _mod2pi(-alpha + tmp)
        q = _mod2pi(beta - tmp)
        out.append(("LSL", t, p, q))

    # RSR
    p_sq = 2.0 + d * d - 2.0 * cos_ab + 2.0 * d * (sin_b - sin_a)
    if p_sq >= -EPS:
        p = math.sqrt(max(p_sq, 0.0))
        tmp = math.atan2(cos_a - cos_b, d - sin_a + sin_b)
        t = _mod2pi(alpha - tmp)
        q = _mod2pi(-beta + tmp)
        out.append(("RSR", t, p, q))

    # LSR
    p_sq = -2.0 + d * d + 2.0 * cos_ab + 2.0 * d * (sin_a + sin_b)
    if p_sq >= -EPS:
        p = math.sqrt(max(p_sq, 0.0))
        tmp = math.atan2(-cos_a - cos_b, d + sin_a + sin_b) - math.atan2(-2.0, p)
        t = _mod2pi(-alpha + tmp)
        q = _mod2pi(-_mod2pi(beta) + tmp)
        out.append(("LSR", t, p, q))

    # RSL
    p_sq = d * d - 2.0 + 2.0 * cos_ab - 2.0 * d * (sin_a + sin_b)
    if p_sq >= -EPS:
        p = math.sqrt(max(p_sq, 0.0))
        tmp = math.atan2(cos_a + cos_b, d - sin_a - sin_b) - math.atan2(2.0, p)
        t = _mod2pi(alpha - tmp)
        q = _mod2pi(beta - tmp)
        out.append(("RSL", t, p, q))

    # RLR
    tmp = (6.0 - d * d + 2.0 * cos_ab + 2.0 * d * (sin_a - sin_b)) / 8.0
    if abs(tmp) <= 1.0 + EPS:
        p = _mod2pi(2.0 * math.pi - math.acos(max(-1.0, min(1.0, tmp))))
        t = _mod2pi(alpha - math.atan2(cos_a - cos_b, d - sin_a + sin_b) + p / 2.0)
        q = _mod2pi(alpha - beta - t + p)
        out.append(("RLR", t, p, q))

    # LRL
    tmp = (6.0 - d * d + 2.0 * cos_ab + 2.0 * d * (sin_b - sin_a)) / 8.0
    if abs(tmp) <= 1.0 + EPS:
        p = _mod2pi(2.0 * math.pi - math.acos(max(-1.0, min(1.0, tmp))))
        t = _mod2pi(-alpha - math.atan2(cos_a - cos_b, d + sin_a - sin_b) + p / 2.0)
        q = _mod2pi(_mod2pi(beta) - alpha - t + _mod2pi(p))
        out.append(("LRL", t, p, q))

    return out


def _dubins_shortest_path(
    sx: float, sy: float, syaw: float, gx: float, gy: float, gyaw: float, radius: float
) -> tuple[str, float, float, float]:
    """Найкоротший з 6 варіантів Dubins-шляху між двома позами. Повертає
    (mode, t, p, q) у нормованих (на radius) одиницях; реальна довжина шляху —
    (|t|+|p|+|q|)*radius."""
    dx, dy = gx - sx, gy - sy
    d = math.hypot(dx, dy) / radius
    theta = _mod2pi(math.atan2(dy, dx))
    alpha = _mod2pi(syaw - theta)
    beta = _mod2pi(gyaw - theta)

    candidates = _dubins_path_candidates(alpha, beta, d)
    if not candidates:
        # Теоретично трапляється лише за майже вироджених вхідних даних
        # (наприклад, d == 0); пряма лінія — розумний резервний варіант.
        return "S", 0.0, d, 0.0
    return min(candidates, key=lambda c: abs(c[1]) + abs(c[2]) + abs(c[3]))


def _pose_after_arc(x: float, y: float, yaw: float, angle: float, radius: float, turn_left: bool) -> tuple[float, float, float]:
    if turn_left:
        new_yaw = yaw + angle
        nx = x + radius * (math.sin(new_yaw) - math.sin(yaw))
        ny = y - radius * (math.cos(new_yaw) - math.cos(yaw))
    else:
        new_yaw = yaw - angle
        nx = x + radius * (math.sin(yaw) - math.sin(new_yaw))
        ny = y + radius * (math.cos(new_yaw) - math.cos(yaw))
    return nx, ny, new_yaw


def _sample_dubins_segment(
    x: float, y: float, yaw: float, letter: str, length_norm: float, radius: float, step_m: float
) -> tuple[list[tuple[float, float]], float, float, float]:
    """Дискретизує один сегмент (L/R/S) шляху на точки з кроком приблизно
    step_m. Повертає (точки без стартової, кінцева поза)."""
    points: list[tuple[float, float]] = []
    if letter == "S":
        length_m = length_norm * radius
        n_steps = max(1, int(length_m / step_m))
        ds = length_m / n_steps
        for _ in range(n_steps):
            x += ds * math.cos(yaw)
            y += ds * math.sin(yaw)
            points.append((x, y))
        return points, x, y, yaw

    # L/R — дуга: length_norm тут це кут у радіанах.
    arc_len_m = abs(length_norm) * radius
    n_steps = max(1, int(arc_len_m / step_m))
    dphi = length_norm / n_steps
    for _ in range(n_steps):
        x, y, yaw = _pose_after_arc(x, y, yaw, dphi, radius, turn_left=(letter == "L"))
        points.append((x, y))
    return points, x, y, yaw


def _dubins_path_points(
    sx: float, sy: float, syaw: float, gx: float, gy: float, gyaw: float, radius: float, step_m: float
) -> list[tuple[float, float]]:
    """Повна дискретизована (у локальних east/north метрах) траєкторія
    найкоротшого Dubins-шляху між двома позами, включно зі стартовою точкою."""
    mode, t, p, q = _dubins_shortest_path(sx, sy, syaw, gx, gy, gyaw, radius)
    points = [(sx, sy)]
    x, y, yaw = sx, sy, syaw
    for letter, length_norm in zip(mode, (t, p, q)):
        seg_points, x, y, yaw = _sample_dubins_segment(x, y, yaw, letter, length_norm, radius, step_m)
        points.extend(seg_points)
    return points


def _chain_dubins_route_local(
    local_points: list[tuple[float, float]], radius_m: float, step_m: float,
) -> tuple[list[tuple[float, float]], list[int]]:
    """
    Спільне ядро для compute_dubins_route (лише для показу на карті) і
    compute_dubins_route_profile (для автопілота): ланцюжок Dubins-шляхів
    через задані точки (уже в локальних east/north метрах). Курс у кожній
    проміжній точці — пеленг на НАСТУПНУ точку (плавний вихід на наступну
    ногу маршруту); в останній точці — той самий курс, що й підхід до неї
    (прямий захід, без розвороту в кінці).

    Повертає (усі точки шляху, boundaries), де boundaries[i] — індекс у
    поверненому списку, що відповідає ТОЧНО local_points[i] (boundaries[0]==0;
    _dubins_path_points завжди закінчується точно в заданій цільовій позі).
    """
    n = len(local_points)
    yaws = [0.0] * n
    for i in range(n - 1):
        dx = local_points[i + 1][0] - local_points[i][0]
        dy = local_points[i + 1][1] - local_points[i][1]
        yaws[i] = math.atan2(dy, dx)
    yaws[n - 1] = yaws[n - 2] if n >= 2 else 0.0

    full_local: list[tuple[float, float]] = [local_points[0]]
    boundaries: list[int] = [0]
    for i in range(n - 1):
        sx, sy = local_points[i]
        gx, gy = local_points[i + 1]
        seg = _dubins_path_points(sx, sy, yaws[i], gx, gy, yaws[i + 1], radius_m, step_m)
        full_local.extend(seg[1:])  # без дублювання стартової точки сегмента
        boundaries.append(len(full_local) - 1)
    return full_local, boundaries


def compute_dubins_route(
    start_latlon: tuple[float, float],
    waypoints: list[tuple[float, float, float]],
    ref_lat: float,
    ref_lon: float,
    radius_m: float,
    step_m: float = DUBINS_SAMPLE_STEP_M,
) -> list[tuple[float, float]]:
    """
    Розраховує повну траєкторію (стартова позиція -> WAYPOINTS[0] -> ... ->
    WAYPOINTS[-1]) як ланцюжок Dubins-шляхів для літака з радіусом віражу
    radius_m. Повертає список (lat, lon) уздовж усієї траєкторії — лише для
    показу на карті (чорний пунктир); висотний профіль для автопілота рахує
    compute_dubins_route_profile нижче.
    """
    all_points_latlon = [start_latlon] + [(lat, lon) for lat, lon, _alt in waypoints]
    local_points = [latlon_to_local_m(lat, lon, ref_lat, ref_lon) for lat, lon in all_points_latlon]
    full_local, _boundaries = _chain_dubins_route_local(local_points, radius_m, step_m)
    return [local_m_to_latlon(east, north, ref_lat, ref_lon) for east, north in full_local]


@dataclass
class RouteProfile:
    """Той самий Dubins-шлях, що й compute_dubins_route, але в локальних
    east/north метрах (для автопілота — без повторних lat/lon перетворень
    щотік) і з висотним профілем + індексами оригінальних WAYPOINTS у шляху
    (для статусу "точку досягнуто" на карті) + кумулятивною довжиною шляху
    (для відстеження прогресу автопілота — інтегрування пройденої відстані,
    а не пошук найближчої точки в просторі, див. PathFollower.update)."""
    local: np.ndarray               # (N, 2) — east, north, метри
    alt: np.ndarray                 # (N,) — висота, метри (лінійна інтерполяція між WAYPOINTS)
    waypoint_indices: list[int]     # для кожної точки WAYPOINTS (без старту) — її індекс у local/alt
    cum_dist: np.ndarray            # (N,) — довжина шляху від старту до точки i, метри


def compute_dubins_route_profile(
    start_pose: tuple[float, float, float],
    waypoints: list[tuple[float, float, float]],
    ref_lat: float,
    ref_lon: float,
    radius_m: float,
    step_m: float = DUBINS_SAMPLE_STEP_M,
) -> RouteProfile:
    start_lat, start_lon, start_alt = start_pose
    all_points_latlon = [(start_lat, start_lon)] + [(lat, lon) for lat, lon, _alt in waypoints]
    all_alts = [start_alt] + [alt for _lat, _lon, alt in waypoints]
    local_points = [latlon_to_local_m(lat, lon, ref_lat, ref_lon) for lat, lon in all_points_latlon]
    full_local, boundaries = _chain_dubins_route_local(local_points, radius_m, step_m)

    alt_profile = np.empty(len(full_local), dtype=np.float64)
    for i in range(len(boundaries) - 1):
        i0, i1 = boundaries[i], boundaries[i + 1]
        alt0, alt1 = all_alts[i], all_alts[i + 1]
        seg_points = full_local[i0:i1 + 1]

        cum_dist = [0.0]
        for k in range(1, len(seg_points)):
            (x0, y0), (x1, y1) = seg_points[k - 1], seg_points[k]
            cum_dist.append(cum_dist[-1] + math.hypot(x1 - x0, y1 - y0))
        total = cum_dist[-1] if cum_dist[-1] > 1e-9 else 1.0

        for k, idx in enumerate(range(i0, i1 + 1)):
            frac = cum_dist[k] / total
            alt_profile[idx] = alt0 + (alt1 - alt0) * frac

    local_arr = np.array(full_local, dtype=np.float64)
    diffs = np.diff(local_arr, axis=0)
    seg_len = np.hypot(diffs[:, 0], diffs[:, 1])
    cum_dist = np.concatenate([[0.0], np.cumsum(seg_len)])

    return RouteProfile(
        local=local_arr,
        alt=alt_profile,
        waypoint_indices=boundaries[1:],  # boundaries[0] == 0 -- це старт, не точка WAYPOINTS
        cum_dist=cum_dist,
    )


def split_into_dash_segments(
    points_latlon: list[tuple[float, float]],
    points_local: list[tuple[float, float]],
    on_m: float = DUBINS_DASH_ON_M,
    off_m: float = DUBINS_DASH_OFF_M,
) -> list[list[tuple[float, float]]]:
    """
    tkintermapview не підтримує пунктирні лінії напряму — імітуємо пунктир,
    розбиваючи суцільну ламану на короткі окремі відрізки ("риски") з
    проміжками, кожен малюється власним set_path(). Перемикання on/off — по
    накопиченій довжині шляху в метрах (points_local — ті самі точки в
    локальних east/north, для рахунку відстані).
    """
    if len(points_latlon) < 2:
        return []

    period = on_m + off_m
    segments: list[list[tuple[float, float]]] = []
    current: list[tuple[float, float]] = [points_latlon[0]]
    cumdist = 0.0

    for i in range(1, len(points_local)):
        (x0, y0), (x1, y1) = points_local[i - 1], points_local[i]
        cumdist += math.hypot(x1 - x0, y1 - y0)
        is_on = (cumdist % period) < on_m
        if is_on:
            current.append(points_latlon[i])
        else:
            if len(current) >= 2:
                segments.append(current)
            current = []
    if len(current) >= 2:
        segments.append(current)
    return segments


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
    кадр було відправлено в YOLO (а не проти "поточних" орієнтирів, чиї
    pixel_x/pixel_y щотік перепроєктуються з уже зміщеної позиції камери).
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
) -> Optional[tuple[float, float, float]]:
    """
    cv2.solvePnPRansac замість "голого" solvePnP: один хибний 2D-3D збіг
    (неправильно зіставлений орієнтир) здатен зіпсувати ввесь розв'язок EPnP.
    RANSAC підбирає найбільшу узгоджену підмножину точок (inliers, поріг
    PNP_REPROJECTION_ERROR_PX) і рахує позу лише по ній, ігноруючи викиди.
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


def _cessna_process_model(
    x: np.ndarray, u: tuple[float, float, float, float], dt: float
) -> np.ndarray:
    """
    Спрощена фізична модель Cessna 172N як точкової маси: стан
    x=[east,north,alt,yaw,speed], керування u=(roll_deg,pitch_deg,
    yaw_rate_deg_s,thrust) — той самий формат, що йде в build_command.

    Знак повороту: build_command/UAttitudeControlComponent — позитивний
    крен/yaw_rate = поворот ПРАВОРУЧ (курс за компасом зростає). Стан x[3]
    тут — МАТЕМАТИЧНИЙ курс (atan2(north,east), CCW-позитивний = ліворуч),
    та сама конвенція, що й у Dubins-плануванні вище (east=x, north=y —
    поворот від осі x до осі y на +90° водночас і математично "проти
    годинникової стрілки", і фізично лівий поворот, тож координати не
    дзеркалимо, а лише міняємо знак похідної курсу: позитивний
    крен/yaw_rate -> ВІД'ЄМНИЙ приріст математичного курсу).
    """
    east, north, alt, yaw, speed = x
    roll_deg, pitch_deg, yaw_rate_deg_s, thrust = u
    roll = math.radians(roll_deg)
    pitch = math.radians(max(-30.0, min(30.0, pitch_deg)))  # запобіжник для sin/cos на випадок
                                                              # аномального (поза автопілотом) pitch

    speed_target = CESSNA172N_MIN_SPEED_MPS + thrust * (CESSNA172N_MAX_SPEED_MPS - CESSNA172N_MIN_SPEED_MPS)
    speed_new = speed + (speed_target - speed) * min(1.0, dt / SPEED_TIME_CONSTANT_S)
    speed_new = max(speed_new, 1.0)  # не дати швидкості піти до нуля/від'ємної (ділення нижче)

    speed_for_turn = max(speed, 1.0)
    coordinated_turn_rate = GRAVITY_MPS2 * math.tan(roll) / speed_for_turn  # рад/с, компасна конвенція
    yaw_rate = -(coordinated_turn_rate + math.radians(yaw_rate_deg_s))      # -> математична конвенція
    yaw_new = yaw + yaw_rate * dt

    horizontal_speed = speed * math.cos(pitch)
    climb_rate = speed * math.sin(pitch)
    east_new = east + horizontal_speed * math.cos(yaw) * dt
    north_new = north + horizontal_speed * math.sin(yaw) * dt
    alt_new = alt + climb_rate * dt

    return np.array([east_new, north_new, alt_new, yaw_new, speed_new], dtype=np.float64)


def _numeric_jacobian(f, x: np.ndarray, eps: float = 1e-5) -> np.ndarray:
    """Якобіан f у точці x чисельним диференціюванням (центральна різниця не
    потрібна — точності цілком достатньо для EKF-коваріації, а код лишається
    коротким і без ризику помилки в ручному виведенні часткових похідних
    нелінійної _cessna_process_model)."""
    n = len(x)
    f0 = f(x)
    jac = np.zeros((len(f0), n))
    for i in range(n):
        step = eps * max(1.0, abs(x[i]))
        dx = np.zeros(n)
        dx[i] = step
        jac[:, i] = (f(x + dx) - f0) / step
    return jac


class CessnaEKF:
    """
    Розширений фільтр Калмана (EKF) для власної позиції/курсу/швидкості
    Cessna 172N. predict() викликається КОЖЕН тік циклу (навіть коли YOLO ще
    не встигло дати новий результат, чи орієнтирів у кадрі взагалі не видно)
    і рухає стан ВПЕРЕД за _cessna_process_model, керованою ОСТАННЬОЮ виданою
    автопілотом командою — тобто це симуляція реакції літака на власні
    керуючі сигнали, а не гола екстраполяція по колишній швидкості. update()
    викликається лише коли є свіжий сирий PnP-фікс (estimate_drone_position)
    і зливає його з передбаченням; ворота на викид (тест Махаланобіса проти
    поточної невизначеності фільтра P) відкидають поодинокий хибний 2D-3D
    збіг, який RANSAC міг не відсіяти.

    Швидкість (x[4]) НЕ лише опосередковано "підтягується" через крос-
    кореляції P разом із позицією (як було раніше — повільно й неточно,
    особливо доки P ще не встигла накопичити ці кореляції): update() ДОДАТКОВО
    рахує |Δпозиція|/Δt між цим і ПОПЕРЕДНІМ прийнятим сирим PnP-фіксом (тобто
    напряму з РЕАЛЬНОГО положення, що приходить) і зливає це як окремий
    скалярний вимір x[4] (_scalar_update) — прямий вимір реальної швидкості,
    а не лише побічний ефект позиційної корекції.
    """

    STATE_DIM = 5

    def __init__(self) -> None:
        self.x: Optional[np.ndarray] = None
        self.P: Optional[np.ndarray] = None
        self._last_control: tuple[float, float, float, float] = (0.0, 0.0, 0.0, CRUISE_THRUST)
        self._last_measurement_time: Optional[float] = None
        self._consecutive_rejections: int = 0
        self._last_raw_measurement: Optional[np.ndarray] = None  # (east,north,alt) останнього
        self._last_raw_time: Optional[float] = None              # ПРИЙНЯТОГО сирого PnP-фіксу —
                                                                   # для прямого |Δпозиція|/Δt (див. update)

    @property
    def initialized(self) -> bool:
        return self.x is not None

    def initialize(
        self, east: float, north: float, alt: float, yaw_rad: float, speed_mps: float, now: float
    ) -> None:
        self.x = np.array([east, north, alt, yaw_rad, speed_mps], dtype=np.float64)
        self.P = np.diag([
            EKF_INITIAL_STD_POS_M ** 2, EKF_INITIAL_STD_POS_M ** 2, EKF_INITIAL_STD_POS_M ** 2,
            math.radians(EKF_INITIAL_STD_YAW_DEG) ** 2, EKF_INITIAL_STD_SPEED_MPS ** 2,
        ])
        self._last_measurement_time = now
        self._consecutive_rejections = 0
        self._last_raw_measurement = np.array([east, north, alt], dtype=np.float64)
        self._last_raw_time = now

    def _scalar_update(self, index: int, measured_value: float, measurement_variance: float) -> None:
        """Класичне скалярне оновлення Калмана для ОДНОГО виміру стану x[index]
        (H — одиничний вектор e_index): y=measured-x[index], S=P[idx,idx]+R,
        K=P[:,idx]/S. Коректно поширює кореляцію на ВЕСЬ стан через повний
        стовпець P[:,index] (а не ізольовано міняє лише x[index]/P[idx,idx])."""
        y = measured_value - self.x[index]
        s = self.P[index, index] + measurement_variance
        k = self.P[:, index] / s
        self.x = self.x + k * y
        self.P = self.P - np.outer(k, self.P[index, :])

    def set_control(self, roll_deg: float, pitch_deg: float, yaw_rate_deg_s: float, thrust: float) -> None:
        self._last_control = (roll_deg, pitch_deg, yaw_rate_deg_s, thrust)

    def predict(self, dt: float) -> None:
        if self.x is None or dt <= 0.0:
            return
        u = self._last_control
        f = lambda xx: _cessna_process_model(xx, u, dt)  # noqa: E731
        F = _numeric_jacobian(f, self.x)
        self.x = f(self.x)
        q = np.array([
            (EKF_PROCESS_STD_POS_MPS ** 2) * dt,
            (EKF_PROCESS_STD_POS_MPS ** 2) * dt,
            (EKF_PROCESS_STD_POS_MPS ** 2) * dt,
            (math.radians(EKF_PROCESS_STD_YAW_DEG_S) ** 2) * dt,
            (EKF_PROCESS_STD_SPEED_MPS ** 2) * dt,
        ])
        self.P = F @ self.P @ F.T + np.diag(q)

    def update(self, measurement: tuple[float, float, float], now: float) -> bool:
        """measurement = (east, north, alt) — сирий (без EMA) вихід
        estimate_drone_position, у тій самій локальній ENU-площині. Повертає
        True, якщо вимір прийнято (пройшов ворота на викид)."""
        if self.x is None:
            return False

        z = np.array(measurement, dtype=np.float64)
        H = np.zeros((3, self.STATE_DIM))
        H[0, 0] = H[1, 1] = H[2, 2] = 1.0
        R = np.diag([EKF_MEASUREMENT_STD_POS_M ** 2, EKF_MEASUREMENT_STD_POS_M ** 2, EKF_MEASUREMENT_STD_ALT_M ** 2])

        y = z - H @ self.x
        S = H @ self.P @ H.T + R
        mahalanobis_sq = float(y.T @ np.linalg.solve(S, y))

        if mahalanobis_sq > EKF_OUTLIER_GATE_CHI2_3DOF:
            self._consecutive_rejections += 1
            if self._consecutive_rejections < EKF_MAX_CONSECUTIVE_REJECTIONS:
                return False  # найімовірніше, поодинокий хибний 2D-3D збіг — лишаємо чисте передбачення

            # EKF_MAX_CONSECUTIVE_REJECTIONS вимірів ПОСПІЛЬ не пройшли ворота
            # — це вже ознака того, що розійшовся САМ ФІЛЬТР (спрощена модель
            # не встигає за реальною фізикою), а не що вимірам не можна
            # довіряти: малий приріст K@y тут не врятує, розбіжність лише
            # накопичувалась би далі. Тому позицію/висоту ПРИМУСОВО скидаємо
            # прямо на вимір (а не змішуємо), і відновлюємо невизначеність
            # P для них — курс/швидкість лишаємо (вимір їх не дає).
            self.x[0], self.x[1], self.x[2] = z[0], z[1], z[2]
            self.P[0:3, :] = 0.0
            self.P[:, 0:3] = 0.0
            self.P[0, 0] = self.P[1, 1] = EKF_INITIAL_STD_POS_M ** 2
            self.P[2, 2] = EKF_MEASUREMENT_STD_ALT_M ** 2
        else:
            K = self.P @ H.T @ np.linalg.inv(S)
            self.x = self.x + K @ y
            self.P = (np.eye(self.STATE_DIM) - K @ H) @ self.P

        self._consecutive_rejections = 0
        self._last_measurement_time = now

        # Пряме обчислення швидкості з РЕАЛЬНОГО положення: |Δпозиція|/Δt між
        # цим і ПОПЕРЕДНІМ прийнятим сирим PnP-фіксом (а не лише опосередкована
        # оцінка через крос-кореляції P, як досі) — і зливається як окремий
        # скалярний вимір x[4] (_scalar_update). Дисперсія цього виміру росте
        # як 1/dt² (похибка позиції в чисельнику ділиться на маленький dt),
        # тож на надто короткій базі (dt < MIN_SPEED_MEAS_DT_S) цей вимір
        # природно майже не впливає на оцінку — а не вносить шум.
        if self._last_raw_measurement is not None:
            dt_meas = now - self._last_raw_time
            if dt_meas > MIN_SPEED_MEAS_DT_S:
                measured_speed = float(np.linalg.norm(z - self._last_raw_measurement)) / dt_meas
                speed_variance = 2.0 * (EKF_MEASUREMENT_STD_POS_M ** 2) / (dt_meas ** 2)
                self._scalar_update(4, measured_speed, speed_variance)
        self._last_raw_measurement = z.copy()
        self._last_raw_time = now

        return True

    def seconds_since_measurement(self, now: float) -> float:
        if self._last_measurement_time is None:
            return math.inf
        return now - self._last_measurement_time

    @property
    def pose_local(self) -> tuple[float, float, float]:
        return float(self.x[0]), float(self.x[1]), float(self.x[2])

    @property
    def yaw_rad(self) -> float:
        return float(self.x[3])

    @property
    def speed_mps(self) -> float:
        return float(self.x[4])


# ── Автопілот: NavCommand, PathFollower (уся логіка — в одному методі update) ─

def _wrap180(angle_deg: float) -> float:
    return (angle_deg + 180.0) % 360.0 - 180.0


def _eval_roll_distance_curve(points: list[tuple[float, float]], x: float) -> float:
    """Кусково-лінійна інтерполяція по контрольних точках (points), відсортованих
    за зростанням x — множник (0..1) на бажаний крен залежно від crosstrack-
    дистанції x=dist_to_nearest_m. За межами діапазону контрольних точок —
    крайнє значення (перше/останнє), БЕЗ екстраполяції за нахилом країв кривої."""
    if not points:
        return 1.0
    if x <= points[0][0]:
        return points[0][1]
    if x >= points[-1][0]:
        return points[-1][1]
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        if x0 <= x <= x1:
            if x1 == x0:
                return y1
            frac = (x - x0) / (x1 - x0)
            return y0 + (y1 - y0) * frac
    return points[-1][1]


@dataclass
class NavCommand:
    """Одна видана команда керування + контекст для логу/статусу (лише дані,
    без логіки — сама логіка вся в PathFollower.update)."""
    roll_deg: float
    pitch_deg: float
    yaw_rate_deg_s: float
    thrust: float
    status: str
    reached_count: int = 0
    target_alt: Optional[float] = None
    dist_to_end_m: Optional[float] = None
    heading_error_deg: Optional[float] = None
    target_vector: Optional[tuple[float, float]] = None  # (east, north) — вектор, що ЗАРАЗ використовується
    target_vector_mode: Optional[str] = None              # "point" (до найближчої точки) чи "path" (вздовж шляху)


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


class PathFollower:
    """
    Автопілот, консолідований в ОДИН метод — update(): приймає інформацію
    про траєкторію (trajectory: RouteProfile — шлях + висотний профіль +
    кумулятивна довжина), про поточне положення (position: east/north/alt) і
    курс (yaw_rad) літака, і про поточну швидкість (speed_mps); повертає
    команду керування (NavCommand). Уся логіка автопілота — прогрес по
    шляху, вибір lookahead-цілі, компенсування похибки курсу, PID висоти,
    модуляція тяги, виявлення прибуття — реалізована ВСЕРЕДИНІ цього одного
    методу. Решта класу — лише стан, що переноситься між викликами (прогрес
    по шляху, інтегратор PID висоти, останні значення для похідних), і легкі
    геттери для зовнішнього звіту (reached_count — для кольору міток
    маршруту на карті).

    Ціль курсу — точка на LOOKAHEAD_TIME_S секунд попереду поточного
    прогресу вздовж шляху (за ПОТОЧНОЮ швидкістю, тому відстань "плаваюча",
    межі LOOKAHEAD_MIN_M/LOOKAHEAD_MAX_M) — "чисте переслідування" (pure
    pursuit). Компенсування похибки курсу — bang-bang закон, скопійований
    (мінімальні зміни) з WaypointNavigator у uav_object_geolocationо.py:
    крен максимальний (±AUTOPILOT_MAX_BANK_DEG), поки похибка курсу до
    lookahead-точки не потрапить у ALIGN_THRESHOLD_DEG, і лише в цьому
    вузькому коридорі спадає до нуля лінійно; yaw_rate — додатковий,
    незалежний від крену канал довороту курсу поверх самого віражу.

    Прогрес уздовж шляху веде НЕ пошуком найближчої точки шляху в просторі,
    а ІНТЕГРУВАННЯМ пройденої відстані (швидкість × dt, спроєктована на
    напрямок шляху), з лише вузькою корективною прив'язкою (+/-
    PATH_CORRECTIVE_WINDOW_POINTS) до найближчої точки шляху щотік. Це
    свідомий вибір: ноги маршруту, коротші за DUBINS_TURN_RADIUS_M (а серед
    WAYPOINTS такі є), змушують Dubins-планувальник будувати петлю, що САМА
    СЕБЕ перетинає у просторі — "найближча точка шляху" без прив'язки до вже
    пройденої відстані регулярно виявлялася б на іншому витку тієї самої
    петлі, і прогрес назавжди "застрягав" у хибному місці.

    Висота тримається одноконтурним PID (інлайн у update, раніше окремий
    клас AltitudeHold) із target_alt, узятим із висотного профілю траєкторії
    в тій самій точці попереду, — тангаж при цьому обмежений вузьким
    коридором PITCH_MAX_UP_DEG..PITCH_MAX_DOWN_DEG: власна позиція
    визначається виключно по орієнтирах у кадрі камери, тож підняти ніс —
    ризик втратити їх з поля зору. Тяга модулюється трьома рівнями
    (CRUISE_THRUST/CLIMB_THRUST/DESCEND_THRUST) залежно від знаку похибки
    висоти — оскільки тангаж угору й так обмежений, компенсувати брак
    набору висоти доводиться саме тягою. Ціль висоти НІКОЛИ не опускається
    нижче RECOVERY_MIN_ALT_M (запобіжник від нескінченного зниження).
    """

    def __init__(self) -> None:
        self._progress_idx = 0
        self._progress_distance_m = 0.0
        self._last_heading_error_deg = 0.0  # для проєкції швидкості на напрямок шляху
        self._altitude_integ = 0.0          # інтегратор PID висоти (анти-віндап)
        self._last_alt: Optional[float] = None
        self._last_time: Optional[float] = None
        self._last_roll_cmd: float = 0.0    # для обмеження швидкості нахилу (roll_rate_limit_deg_s)
        self._final_reached = False         # див. done/mission complete — остання точка не через індекс
        self._waypoint_indices: list[int] = []  # оновлюється щовиклику update() з trajectory.waypoint_indices

    @property
    def reached_count(self) -> int:
        indices = self._waypoint_indices
        if not indices:
            return 0
        count = sum(1 for idx in indices[:-1] if idx <= self._progress_idx)
        if self._final_reached:
            count += 1
        return count

    @property
    def done(self) -> bool:
        # НЕ вимагає, щоб прогрес-індекс влучив ТОЧНО в останню точку шляху:
        # останні ноги маршруту (коротші за DUBINS_TURN_RADIUS_M) дають
        # Dubins-петлю, що самоперетинається біля самого фінішу. Досить, що
        # всі точки маршруту ДО останньої вже пройдено — саму останню
        # перевіряє dist_to_end_m <= ARRIVAL_RADIUS_M (пряма відстань, не
        # залежить від індексу).
        indices = self._waypoint_indices
        if len(indices) < 2:
            return True
        return self._progress_idx >= indices[-2]

    def update(
        self,
        trajectory: RouteProfile,
        position: tuple[float, float, float],
        yaw_rad: float,
        speed_mps: float,
        attack_vector: tuple[float, float],
        roll_gain: float,
        pitch_gain: float,
        tail_threshold_deg: float,
        switch_distance_m: float,
        roll_rate_limit_deg_s: float,
        alignment_lead_time_s: float,
        roll_distance_curve: list[tuple[float, float]],
        now: float,
    ) -> NavCommand:
        """roll_gain/pitch_gain/tail_threshold_deg/switch_distance_m/
        roll_rate_limit_deg_s — п'ять ползунків з AppWindow (get_tuning()):
        сила крена й підйому керма висоти залежно від кута, крен, з якого
        вмикається хвіст (yaw_rate), відстань, ближче якої вектор до
        найближчої точки замінюється вектором руху вздовж траєкторії, і
        максимальна швидкість нахилу (град/с — крен не може змінитись
        швидше, ніж дозволяє це обмеження). alignment_lead_time_s — шостий
        числовий ползунок: на скільки секунд наближення "наперед" зазирати
        при оцінці ефективної дистанції для roll_distance_curve (нижче) —
        щоб вирівнювання (спад крена) починалось РАНІШЕ при більшій швидкості
        наближення, а не лише коли літак ВЖЕ близько. roll_distance_curve —
        сьомий (не-числовий) "ползунок" AppWindow: контрольні точки кривої
        (ефективна дистанція, м -> множник 0..1) на бажаний крен, редагована
        мишею у вікні — див. _eval_roll_distance_curve."""
        east, north, alt = position

        dt = 0.0 if self._last_time is None else max(0.0, now - self._last_time)
        self._last_time = now

        # 1. Прогрес уздовж траєкторії — ІНТЕГРУВАННЯ пройденої відстані
        # (швидкість × dt, спроєктована на напрямок шляху похибкою курсу з
        # ПОПЕРЕДНЬОГО тіку: self._last_heading_error_deg), а НЕ голий пошук
        # найближчої точки по ВСЬОМУ масиву траєкторії. Короткі ноги
        # маршруту (коротші за DUBINS_TURN_RADIUS_M) змушують Dubins-
        # планувальник будувати петлю, що сама себе перетинає у просторі —
        # "найближча точка" без прив'язки до вже пройденої відстані регулярно
        # "перестрибувала" б на інший виток тієї самої петлі, і прогрес
        # назавжди "застрягав" би у хибному місці. Вузьке вікно
        # (+/- PATH_CORRECTIVE_WINDOW_POINTS точок навколо передбаченого
        # інтегруванням індексу) лишається лише для корекції крос-трек
        # відхилення від передбачення, а не для пошуку по всій траєкторії.
        predicted_progress_m = self._progress_distance_m + speed_mps * dt * math.cos(
            math.radians(self._last_heading_error_deg)
        )
        predicted_progress_m = max(0.0, min(predicted_progress_m, float(trajectory.cum_dist[-1])))
        predicted_idx = min(
            int(np.searchsorted(trajectory.cum_dist, predicted_progress_m)), len(trajectory.local) - 1
        )
        window_lo = max(0, predicted_idx - PATH_CORRECTIVE_WINDOW_POINTS)
        window_hi = min(len(trajectory.local), predicted_idx + PATH_CORRECTIVE_WINDOW_POINTS + 1)
        window_local = trajectory.local[window_lo:window_hi]
        d2 = (window_local[:, 0] - east) ** 2 + (window_local[:, 1] - north) ** 2
        nearest_idx = window_lo + int(np.argmin(d2))

        self._progress_idx = nearest_idx
        self._progress_distance_m = float(trajectory.cum_dist[nearest_idx])
        self._waypoint_indices = trajectory.waypoint_indices
        target_east, target_north = trajectory.local[nearest_idx]
        target_alt = float(trajectory.alt[nearest_idx])

        # 2. Вектор помилки — від поточної позиції до найближчої точки.
        error_east = target_east - east
        error_north = target_north - north
        dist_to_nearest_m = math.hypot(error_east, error_north)

        # Вектор РУХУ вздовж траєкторії — від найближчої точки до наступної
        # (напрямок, куди веде сам шлях у цьому місці). Використовується
        # ЗАМІСТЬ вектора до найближчої точки, коли літак уже досить близько
        # до неї (dist_to_nearest_m <= switch_distance_m) — інакше на
        # наближенні до самої точки помилка (і кут) вироджується в майже
        # нульовий вектор і літак почав би "зупинятися" на ній, замість
        # продовжувати рух по траєкторії.
        next_idx = min(nearest_idx + 1, len(trajectory.local) - 1)
        motion_east = trajectory.local[next_idx][0] - target_east
        motion_north = trajectory.local[next_idx][1] - target_north

        if dist_to_nearest_m <= switch_distance_m and (motion_east != 0.0 or motion_north != 0.0):
            target_vector_east, target_vector_north, target_mode = motion_east, motion_north, "path"
        else:
            target_vector_east, target_vector_north, target_mode = error_east, error_north, "point"

        # 3. Кут між вектором атаки літака й цільовим вектором (до точки або
        # вздовж траєкторії — див. вище). Знак через 2D-крос-добуток
        # (target_vector × attack): позитивний кут — ціль ПРАВОРУЧ (курс за
        # компасом треба збільшити), негативний — ЛІВОРУЧ. Це той самий знак,
        # що й у build_command/UAttitudeControlComponent: позитивний крен =
        # поворот праворуч.
        attack_east, attack_north = attack_vector
        cross = target_vector_east * attack_north - target_vector_north * attack_east
        dot = attack_east * target_vector_east + attack_north * target_vector_north
        angle_deg = math.degrees(math.atan2(cross, dot))
        self._last_heading_error_deg = angle_deg  # для проєкції прогресу наступного тіку (див. п.1 вище)

        # 5. Керуючі сигнали з кута й налаштувань.
        desired_roll_cmd = max(-AUTOPILOT_MAX_BANK_DEG, min(AUTOPILOT_MAX_BANK_DEG, roll_gain * angle_deg))

        # Множник (0..1) на бажаний крен від ЕФЕКТИВНОЇ дистанції — чим ближче
        # літак до самої лінії шляху, тим менший крен (різкий віраж біля
        # точного вирівнювання лише розгойдує літак навколо траєкторії, замість
        # покращення точності). "Ефективна" — а не сира dist_to_nearest_m (п.2
        # вище) — тому що при більшій швидкості наближення (closing_speed_mps:
        # складова швидкості САМЕ в напрямку цілі, від'ємну — рух ВІД цілі —
        # ігноруємо) вирівнювання має початись РАНІШЕ, на більшій реальній
        # дистанції: інакше, поки крен фактично спаде (обмежено
        # roll_rate_limit_deg_s), літак на високій швидкості встигає
        # проскочити далі, ніж на низькій. alignment_lead_time_s (секунди) —
        # наскільки "наперед" за часом зазирати; 0 — поведінка як без
        # випередження (сира дистанція).
        closing_speed_mps = max(0.0, speed_mps * math.cos(math.radians(angle_deg)))
        effective_distance_m = max(0.0, dist_to_nearest_m - closing_speed_mps * alignment_lead_time_s)
        roll_distance_mult = _eval_roll_distance_curve(roll_distance_curve, effective_distance_m)
        desired_roll_cmd *= roll_distance_mult

        # Швидкість нахилу: крен не може змінитись за один тік більше, ніж на
        # roll_rate_limit_deg_s * dt (град) — імітує реальну швидкість, з якою
        # літак фізично нахиляється, замість миттєвого стрибка до бажаного крену
        # (dt — той самий, розрахований на початку методу для інтегрування
        # прогресу вздовж траєкторії).
        if dt > 0.0:
            max_step = roll_rate_limit_deg_s * dt
            roll_cmd = self._last_roll_cmd + max(-max_step, min(max_step, desired_roll_cmd - self._last_roll_cmd))
        else:
            # Немає часу, що минув (перший виклик або той самий tick) —
            # тримаємо попередній крен, а НЕ стрибаємо одразу до цілі
            # (інакше саме перший тік обходив би обмеження швидкості нахилу).
            roll_cmd = self._last_roll_cmd
        self._last_roll_cmd = roll_cmd

        pitch_cmd = max(0.0, min(PITCH_MAX_UP_DEG, pitch_gain * abs(angle_deg)))
        if abs(roll_cmd) >= tail_threshold_deg:
            yaw_rate_cmd = math.copysign(MAX_YAW_RATE_DEG_S, roll_cmd)
        else:
            yaw_rate_cmd = 0.0
        thrust = CRUISE_THRUST

        end_east, end_north = trajectory.local[-1]
        dist_to_end_m = math.hypot(end_east - east, end_north - north)

        status = (f"nearest={nearest_idx}  progress={self._progress_distance_m:.0f}/{trajectory.cum_dist[-1]:.0f}m  "
                  f"vector={target_mode}  dist_nearest={dist_to_nearest_m:5.1f}m  "
                  f"eff_dist={effective_distance_m:5.1f}m(lead={alignment_lead_time_s:.1f}s)  "
                  f"roll_mult={roll_distance_mult:.2f}  "
                  f"angle={angle_deg:+6.1f}°  roll={roll_cmd:+5.1f} (ціль {desired_roll_cmd:+5.1f})  "
                  f"pitch={pitch_cmd:+4.1f}  yaw_rate={yaw_rate_cmd:+5.1f}  "
                  f"(gains: roll={roll_gain:.2f} pitch={pitch_gain:.3f} tail>={tail_threshold_deg:.0f}° "
                  f"switch<={switch_distance_m:.0f}m roll_rate<={roll_rate_limit_deg_s:.0f}°/s)")

        return NavCommand(
            roll_cmd, pitch_cmd, yaw_rate_cmd, thrust, status,
            reached_count=self.reached_count, target_alt=target_alt,
            dist_to_end_m=dist_to_end_m, heading_error_deg=angle_deg,
            target_vector=(target_vector_east, target_vector_north), target_vector_mode=target_mode,
        )


# ── Єдине вікно: відео (зліва) + карта (справа) ──────────────────────────────

# Кольори маркера точки маршруту залежно від статусу: (заливка кола, обвід).
_WAYPOINT_COLORS = {
    "pending": ("#9e9e9e", "#616161"),
    "current": ("#e53935", "#b71c1c"),
    "reached": ("#43a047", "#2e7d32"),
}
_DRONE_COLOR = ("#1e88e5", "#0d47a1")   # синя лінія/мітка — наша PnP/EKF-оцінка позиції
_REAL_PATH_COLOR = "#00c853"            # зелена лінія — реальна (симуляційна) позиція,
                                         # GeoPositionDroneComponent, лише для звірки на око
_REAL_COLOR = (_REAL_PATH_COLOR, "#1b5e20")  # заливка/обвід зеленої мітки поточної реальної позиції
_ATTACK_VECTOR_COLOR = "#ff6f00"  # оранжева стрілка — вектор атаки (напрямок польоту) літака
ATTACK_VECTOR_LENGTH_M: float = 150.0  # довжина стрілки на карті, метри
_TARGET_VECTOR_COLOR = "#00e5ff"  # блакитна стрілка — вектор траєкторії (target_vector: до точки/вздовж шляху)


class _CurveEditor(tk.Canvas):
    """
    Мінімальний редактор кусково-лінійної кривої на Canvas — шостий "ползунок"
    AppWindow (крен × crosstrack-дистанція, див. ROLL_DISTANCE_CURVE_MAX_M /
    _eval_roll_distance_curve). Контрольні точки мають ФІКСОВАНИЙ X (рівномірно
    від 0 до x_max) — драгабельний лише Y (0..1); це свідоме спрощення: точки
    ніколи не міняються місцями одна з одною, тож не треба возитись з
    переупорядкуванням під час перетягування — найпростіший надійний UI для
    монотонно зростаючої "дистанція -> множник" кривої.

    on_change(points) викликається на КОЖЕН рух миші під час перетягування (як
    і command в tk.Scale) зі свіжою копією точок [(x, y), ...] — власник
    (AppWindow._on_curve_change) одразу застосовує її як живе значення й
    ставить у чергу відкладений запис на диск (той самий дебаунс-механізм, що
    й для звичайних числових ползунків).
    """

    _MARGIN_LEFT = 26
    _MARGIN_RIGHT = 8
    _MARGIN_TOP = 6
    _MARGIN_BOTTOM = 16
    _POINT_RADIUS = 5
    _HIT_RADIUS = 10

    def __init__(self, parent, width: int, height: int, x_max: float,
                 points: list[tuple[float, float]], on_change) -> None:
        super().__init__(parent, width=width, height=height, bg="#1a1a1a", highlightthickness=0)
        self._width = width
        self._height = height
        self._x_max = x_max
        self._points: list[list[float]] = [[x, y] for x, y in points]  # мутуються на місці під час драгу
        self._on_change = on_change
        self._dragging: Optional[int] = None
        self.bind("<Button-1>", self._on_press)
        self.bind("<B1-Motion>", self._on_drag)
        self.bind("<ButtonRelease-1>", self._on_release)
        self._redraw()

    def _plot_rect(self) -> tuple[float, float, float, float]:
        return (
            self._MARGIN_LEFT, self._MARGIN_TOP,
            self._width - self._MARGIN_RIGHT, self._height - self._MARGIN_BOTTOM,
        )

    def _to_canvas(self, x: float, y: float) -> tuple[float, float]:
        x0, y0, x1, y1 = self._plot_rect()
        cx = x0 + (x / self._x_max) * (x1 - x0) if self._x_max > 0 else x0
        cy = y1 - max(0.0, min(1.0, y)) * (y1 - y0)
        return cx, cy

    def _canvas_y_to_value(self, cy: float) -> float:
        _, y0, _, y1 = self._plot_rect()
        frac = (y1 - cy) / (y1 - y0) if y1 != y0 else 0.0
        return max(0.0, min(1.0, frac))

    def _redraw(self) -> None:
        self.delete("all")
        x0, y0, x1, y1 = self._plot_rect()
        self.create_rectangle(x0, y0, x1, y1, outline="#444444")
        for frac, label in ((0.0, "0"), (1.0, "1")):
            _, cy = self._to_canvas(0.0, frac)
            self.create_line(x0, cy, x1, cy, fill="#2a2a2a")
            self.create_text(x0 - 4, cy, text=label, fill="#888888", font=("Consolas", 7), anchor="e")
        self.create_text(x0, y1 + 8, text="0", fill="#888888", font=("Consolas", 7), anchor="n")
        self.create_text(x1, y1 + 8, text=f"{self._x_max:.0f}м", fill="#888888", font=("Consolas", 7), anchor="n")

        coords: list[float] = []
        for x, y in self._points:
            cx, cy = self._to_canvas(x, y)
            coords.extend((cx, cy))
        if len(coords) >= 4:
            self.create_line(*coords, fill="#00e5ff", width=2)
        for x, y in self._points:
            cx, cy = self._to_canvas(x, y)
            r = self._POINT_RADIUS
            self.create_oval(cx - r, cy - r, cx + r, cy + r, fill="#00e5ff", outline="#003544")

    def _nearest_point_index(self, canvas_x: float, canvas_y: float) -> Optional[int]:
        best_idx, best_dist = None, math.inf
        for i, (x, y) in enumerate(self._points):
            cx, cy = self._to_canvas(x, y)
            d = math.hypot(cx - canvas_x, cy - canvas_y)
            if d < best_dist:
                best_dist, best_idx = d, i
        if best_idx is not None and best_dist <= self._HIT_RADIUS:
            return best_idx
        return None

    def _on_press(self, event: "tk.Event") -> None:
        self._dragging = self._nearest_point_index(event.x, event.y)

    def _on_drag(self, event: "tk.Event") -> None:
        if self._dragging is None:
            return
        self._points[self._dragging][1] = self._canvas_y_to_value(event.y)
        self._redraw()
        self._on_change([(x, y) for x, y in self._points])

    def _on_release(self, _event: "tk.Event") -> None:
        self._dragging = None


class AppWindow:
    """
    Єдине Tkinter-вікно, поділене ГОРИЗОНТАЛЬНО на дві частини: згори — карта
    (tkintermapview, рельси OpenStreetMap) на всю ширину; знизу — смуга з
    трьох колонок (зліва направо): ползунки налаштування автопілоту,
    текстова інформаційна панель (info_label — позиція літака/NAV-статус/
    орієнтири/fps; раніше малювалась ПОВЕРХ кадру через cv2.putText, тепер
    окремим текстовим блоком) і відео (кадр з розміткою об'єктів від
    draw_and_report, конвертований у Tk-зображення через Pillow). Раніше це
    були два окремих вікна (cv2.imshow + окреме Tkinter-вікно карти) — тепер
    вся візуалізація в одному вікні.

    Tkinter не потокобезпечний для довільних викликів з чужого потоку, тож усе
    вікно (root, відео-Label, map_widget, маркери) живе й будується ЛИШЕ у
    власному фоновому потоці (self._thread). Головний цикл (ZMQ + YOLO + PnP)
    викликає лише update(), яка кладе новий стан у чергу (queue.Queue, розмір
    1) — сам віджет вона ніколи не торкається напряму.
    """

    POLL_MS = 15  # частіше за MapView (300мс) — це вікно ще й показує відео

    def __init__(self, waypoints: list[tuple[float, float, float]]) -> None:
        if TkinterMapView is None:
            raise RuntimeError("tkintermapview не встановлено: pip install tkintermapview")

        self._waypoints = waypoints
        # (кадр BGR|None, drone_pose|None, real_pose|None, planned_route_dashes|None, current_index,
        #  info_text, attack_vector|None, target_vector|None)
        QueueItem = tuple[
            Optional[np.ndarray],
            Optional[tuple[float, float, float]],
            Optional[tuple[float, float, float]],
            Optional[list[list[tuple[float, float]]]],
            int,
            str,
            Optional[tuple[float, float]],
            Optional[tuple[float, float]],
        ]
        self._queue: "queue.Queue[QueueItem]" = queue.Queue(maxsize=1)
        self._running = True
        self._ready = threading.Event()
        self.quit_event = threading.Event()  # 'q'/закриття вікна -> сигнал головному циклу завершитись
        self.restart_event = threading.Event()  # кнопка "Рестарт місії" -> сигнал головному циклу скинути EKF/автопілот

        self._wp_markers: list = []
        self._wp_status: list[str] = ["current" if i == 0 else "pending" for i in range(len(waypoints))]
        self._path_points: list[tuple[float, float]] = []
        self._path_line = None
        self._drone_marker = None
        self._real_path_points: list[tuple[float, float]] = []
        self._real_path_line = None
        self._real_marker = None
        self._attack_vector_line = None  # оранжева стрілка — напрямок польоту (attack_vector)
        self._target_vector_line = None  # блакитна стрілка — вектор траєкторії, що зараз використовується
        self._planned_route_drawn = False
        self._planned_route_paths: list = []

        # П'ять ползунків налаштування автопілоту — живі значення, читаються
        # напряму (прості float, безпечно для простого читання з іншого
        # потоку) через get_tuning() з головного циклу; оновлюються callback'ом
        # відповідного tk.Scale у потоці вікна (_set_tuning), який заразом
        # ставить у чергу відкладений запис у TUNING_SETTINGS_PATH
        # (_schedule_save_tuning) — підібране налаштування переживає перезапуск.
        _saved_tuning = self._load_tuning()
        self.roll_gain: float = float(_saved_tuning.get("roll_gain", 1.0))            # сила крена залежно від кута (град крена на град кута)
        self.pitch_gain: float = float(_saved_tuning.get("pitch_gain", 0.05))         # сила підняття керма висоти вгору залежно від кута
        self.tail_threshold_deg: float = float(_saved_tuning.get("tail_threshold_deg", 30.0))  # крен, з якого починає використовуватись хвіст (yaw_rate)
        self.switch_distance_m: float = float(_saved_tuning.get("switch_distance_m", 50.0))    # відстань до найближчої точки, ближче якої вектор до неї
                                                                                                 # замінюється вектором руху вздовж траєкторії
        self.roll_rate_limit_deg_s: float = float(_saved_tuning.get("roll_rate_limit_deg_s", 60.0))  # швидкість нахилу — крен не змінюється швидше за це

        # Шостий, не-числовий "ползунок": крива крен × crosstrack-дистанція
        # (dist_to_nearest_m -> множник 0..1 на бажаний крен, PathFollower.
        # update п.5) — контрольні точки, редаговані мишею (_CurveEditor,
        # створюється нижче в _run). Формат у TUNING_SETTINGS_PATH — список
        # [x, y]-пар; довжина МАЄ збігатись із DEFAULT_ROLL_DISTANCE_CURVE,
        # інакше (стара версія файлу, ручне редагування, пошкодження) —
        # відкат на дефолтну криву, а не часткове/помилкове застосування.
        _saved_curve = _saved_tuning.get("roll_distance_curve")
        if isinstance(_saved_curve, list) and len(_saved_curve) == len(DEFAULT_ROLL_DISTANCE_CURVE):
            try:
                self.roll_distance_curve: list[tuple[float, float]] = [(float(x), float(y)) for x, y in _saved_curve]
            except (TypeError, ValueError):
                self.roll_distance_curve = list(DEFAULT_ROLL_DISTANCE_CURVE)
        else:
            self.roll_distance_curve = list(DEFAULT_ROLL_DISTANCE_CURVE)

        # Шостий ЧИСЛОВИЙ ползунок: наскільки секунд наближення "наперед"
        # зазирати при оцінці ефективної дистанції для roll_distance_curve —
        # щоб вирівнювання (спад крена) починалось раніше при більшій
        # швидкості наближення до траєкторії (PathFollower.update, п.5).
        self.alignment_lead_time_s: float = float(
            _saved_tuning.get("alignment_lead_time_s", DEFAULT_ALIGNMENT_LEAD_TIME_S)
        )

        self._save_tuning_after_id: Optional[str] = None  # хендл відкладеного запису (дебаунс під час перетягування)
        self._video_photo = None  # тримати посилання, інакше Tkinter/Pillow приберуть картинку

        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=10.0)

    # ── Фоновий потік вікна — усе, що торкається Tkinter, лише тут ──────────

    def _run(self) -> None:
        self.root = tk.Tk()
        self.root.title("UAV object geolocation")
        self.root.geometry("1500x1080")
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.bind("<Key>", self._on_key)

        # Верхня частина (горизонтальний поділ навпіл) — карта на всю ширину.
        map_frame = tk.Frame(self.root)
        map_frame.pack(side="top", fill="both", expand=True)
        self.map_widget = TkinterMapView(map_frame, corner_radius=0)
        self.map_widget.pack(fill="both", expand=True)

        # Нижня частина — смуга з трьох колонок: ползунки / інформація / відео.
        # Висота фіксована ЯВНО (а не виводиться з video_frame, як спочатку) —
        # щоб вистачило місця під 5 ползунків + редактор кривої в колонці 1;
        # відеокадр (640x480) при цьому просто трохи "летербоксується" зверху
        # в трохи вищому video_frame, сам кадр не спотворюється (Label не
        # масштабує зображення під розмір віджета).
        bottom_height = 640
        bottom_frame = tk.Frame(self.root, bg="black", height=bottom_height)
        bottom_frame.pack(side="bottom", fill="x", expand=False)
        bottom_frame.pack_propagate(False)

        # Колонка 1: п'ять числових ползунків + редактор кривої "крен ×
        # crosstrack-дистанція" (шостий, не-числовий "ползунок" — див.
        # _CurveEditor). Фіксована ширина + pack_propagate(False), щоб
        # елементи не стискались до ширини тексту.
        controls_width = 340
        controls_frame = tk.Frame(bottom_frame, width=controls_width, bg="black")
        controls_frame.pack(side="left", fill="y", expand=False)
        controls_frame.pack_propagate(False)
        self._make_slider(
            controls_frame, "Крен / кут (roll_gain, град/град)", 0.0, 3.0, 0.05,
            self.roll_gain, lambda v: self._set_tuning("roll_gain", v),
        )
        self._make_slider(
            controls_frame, "Кермо висоти вгору / кут (pitch_gain, град/град)", 0.0, 0.2, 0.005,
            self.pitch_gain, lambda v: self._set_tuning("pitch_gain", v),
        )
        self._make_slider(
            controls_frame, "Поріг крена для хвоста (tail_threshold_deg, °)", 0.0, AUTOPILOT_MAX_BANK_DEG, 1.0,
            self.tail_threshold_deg, lambda v: self._set_tuning("tail_threshold_deg", v),
        )
        self._make_slider(
            controls_frame, "Відстань переходу на вектор траєкторії (switch_distance_m, м)", 0.0, 150.0, 1.0,
            self.switch_distance_m, lambda v: self._set_tuning("switch_distance_m", v),
        )
        self._make_slider(
            controls_frame, "Швидкість нахилу (roll_rate_limit_deg_s, °/с)", 1.0, 180.0, 1.0,
            self.roll_rate_limit_deg_s, lambda v: self._set_tuning("roll_rate_limit_deg_s", v),
        )
        self._make_slider(
            controls_frame, "Випередження вирівнювання (alignment_lead_time_s, с)", 0.0, 5.0, 0.1,
            self.alignment_lead_time_s, lambda v: self._set_tuning("alignment_lead_time_s", v),
        )

        # Сьомий "ползунок" — крива крен × crosstrack-дистанція (чим ближче
        # літак до лінії шляху, тим менший крен): точки тягаються мишею
        # (лише по вертикалі — X фіксований), одразу застосовується "наживо"
        # через _on_curve_change (той самий дебаунс-запис на диск, що й для
        # числових ползунків).
        tk.Label(
            controls_frame, text="Крен × ефективна дистанція (множник)",
            bg="black", fg="#e0e0e0", font=("Consolas", 8), anchor="w",
        ).pack(fill="x", padx=6, pady=(8, 0))
        self._curve_editor = _CurveEditor(
            controls_frame, width=controls_width - 16, height=120,
            x_max=ROLL_DISTANCE_CURVE_MAX_M, points=self.roll_distance_curve,
            on_change=self._on_curve_change,
        )
        self._curve_editor.pack(padx=6, pady=(2, 6))

        # Кнопка рестарту місії: скидає ЛИШЕ стан місії/автопілота (EKF,
        # PathFollower, запланована траєкторія, прогрес WAYPOINTS на карті) —
        # ZMQ-з'єднання, YOLO та підібрані ползунки/крива НЕ чіпаються (див.
        # _on_restart_click/_reset_display і обробку restart_event у main()).
        tk.Button(
            controls_frame, text="⟲ Рестарт місії", command=self._on_restart_click,
            bg="#b71c1c", fg="white", activebackground="#d32f2f", activeforeground="white",
            relief="flat", font=("Consolas", 9, "bold"),
        ).pack(fill="x", padx=6, pady=(10, 4))

        # Колонка 2: текстовий блок з головною інформацією (позиція літака,
        # NAV-статус, к-сть орієнтирів, fps) — раніше малювався ПОВЕРХ кадру
        # (cv2.putText), тепер окремою панеллю поруч, щоб не заступати картинку.
        info_frame = tk.Frame(bottom_frame, bg="black")
        info_frame.pack(side="left", fill="both", expand=True)
        self.info_label = tk.Label(
            info_frame, text="", bg="black", fg="#e0e0e0", font=("Consolas", 10),
            justify="left", anchor="nw", padx=8, pady=6,
        )
        self.info_label.pack(fill="both", expand=True)
        # Ця колонка розтягується (між ползунками й відео), тож wraplength
        # підлаштовується під фактичну ширину при зміні розміру вікна, а не
        # рахується один раз від фіксованої ширини, як було раніше.
        self.info_label.bind(
            "<Configure>", lambda e: self.info_label.configure(wraplength=max(100, e.width - 16)),
        )

        # Колонка 3: відео з камери (розмітка YOLO/орієнтирів, draw_and_report).
        # Висота НЕ фіксується тут (як раніше) — колонка розтягується через
        # fill="y" до bottom_height, кадр (640x480) лишається як є, зверху
        # можливе трохи чорного поля, якщо bottom_height > IMG_H.
        video_frame = tk.Frame(bottom_frame, width=IMG_W, bg="black")
        video_frame.pack(side="left", fill="y", expand=False)
        video_frame.pack_propagate(False)
        self.video_label = tk.Label(video_frame, bg="black")
        self.video_label.pack(fill="both", expand=True)

        for i, (lat, lon, _alt) in enumerate(self._waypoints):
            self._wp_markers.append(self._make_waypoint_marker(i, lat, lon, self._wp_status[i]))

        if len(self._waypoints) >= 2:
            # fit_bounding_box вимагає СПРАВЖНІй прямокутник (top-left != bottom-right).
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

    def _on_close(self) -> None:
        self.quit_event.set()

    def _on_key(self, event: "tk.Event") -> None:
        if event.char == "q":
            self.quit_event.set()

    def _on_restart_click(self) -> None:
        """Кнопка "Рестарт місії" (controls_frame): Button.command вже
        виконується в потоці вікна, тож весь малюнок на карті скидається тут
        же напряму (_reset_display), а не через чергу update(). restart_event
        лише сигналізує ГОЛОВНОМУ циклу (main(), інший потік) скинути СВІЙ
        стан (EKF/PathFollower/запланована траєкторія) — саме він володіє
        цими об'єктами, вікно їх не бачить."""
        self._reset_display()
        self.restart_event.set()

    def _reset_display(self) -> None:
        """Повертає карту в стан "місію ще не почато": мітки WAYPOINTS назад
        у сіро/червоний (як при першому запуску), стирає пройдений реальний
        шлях + його поточну мітку, заплановану Dubins-траєкторію (буде
        перемальована наново, щойно main() пришле нову planned_route після
        повторної ініціалізації) і стрілки attack/target vector. Ползунки/
        крива НЕ чіпаються — це налаштування автопілота, а не стан місії."""
        for marker in self._wp_markers:
            marker.delete()
        self._wp_status = ["current" if i == 0 else "pending" for i in range(len(self._waypoints))]
        self._wp_markers = [
            self._make_waypoint_marker(i, lat, lon, self._wp_status[i])
            for i, (lat, lon, _alt) in enumerate(self._waypoints)
        ]

        self._real_path_points = []
        if self._real_path_line is not None:
            self._real_path_line.delete()
            self._real_path_line = None
        if self._real_marker is not None:
            self._real_marker.delete()
            self._real_marker = None

        for path in self._planned_route_paths:
            path.delete()
        self._planned_route_paths = []
        self._planned_route_drawn = False

        if self._attack_vector_line is not None:
            self._attack_vector_line.delete()
            self._attack_vector_line = None
        if self._target_vector_line is not None:
            self._target_vector_line.delete()
            self._target_vector_line = None

        self.info_label.configure(text="")

    @staticmethod
    def _make_slider(parent, label: str, from_: float, to: float, resolution: float,
                      default: float, on_change) -> "tk.Scale":
        tk.Label(parent, text=label, bg="black", fg="#e0e0e0", font=("Consolas", 8), anchor="w").pack(
            fill="x", padx=6,
        )
        scale = tk.Scale(
            parent, from_=from_, to=to, resolution=resolution, orient=tk.HORIZONTAL,
            bg="black", fg="#e0e0e0", troughcolor="#333333", highlightthickness=0,
            command=lambda v: on_change(float(v)),
        )
        scale.set(default)
        scale.pack(fill="x", padx=6)
        return scale

    @staticmethod
    def _load_tuning() -> dict:
        """Читає TUNING_SETTINGS_PATH (якщо файл є й валідний) — значення
        ползунків, збережені попереднім запуском. Викликається з __init__,
        ДО створення self.root (тобто до фонового потоку вікна), тож це
        звичайний статичний метод без залежності на стан вікна."""
        try:
            with TUNING_SETTINGS_PATH.open("r", encoding="utf-8") as f:
                data = json.load(f)
            return data if isinstance(data, dict) else {}
        except (OSError, json.JSONDecodeError):
            return {}

    def _set_tuning(self, attr: str, value: float) -> None:
        """callback tk.Scale: оновлює живе значення й ставить у чергу
        відкладений запис на диск (_schedule_save_tuning) — виконується в
        потоці вікна (self._thread), як і сам _run/tk.Scale callback."""
        setattr(self, attr, value)
        self._schedule_save_tuning()

    def _on_curve_change(self, points: list[tuple[float, float]]) -> None:
        """callback _CurveEditor (аналог _set_tuning для числових ползунків):
        points — СВІЖА копія контрольних точок, тож просте присвоєння цілого
        списку (без мутації на місці) лишається атомарним і безпечним для
        читання з головного циклу (get_tuning) з іншого потоку."""
        self.roll_distance_curve = points
        self._schedule_save_tuning()

    def _schedule_save_tuning(self) -> None:
        """Дебаунс запису: перетягування ползунка викликає callback десятки
        разів на секунду — записуємо на диск лише раз, через
        TUNING_SAVE_DEBOUNCE_MS після ОСТАННЬОЇ зміни, а не на кожен рух."""
        if self._save_tuning_after_id is not None:
            self.root.after_cancel(self._save_tuning_after_id)
        self._save_tuning_after_id = self.root.after(TUNING_SAVE_DEBOUNCE_MS, self._save_tuning_now)

    def _save_tuning_now(self) -> None:
        self._save_tuning_after_id = None
        data = {
            "roll_gain": self.roll_gain, "pitch_gain": self.pitch_gain,
            "tail_threshold_deg": self.tail_threshold_deg, "switch_distance_m": self.switch_distance_m,
            "roll_rate_limit_deg_s": self.roll_rate_limit_deg_s,
            "alignment_lead_time_s": self.alignment_lead_time_s,
            "roll_distance_curve": self.roll_distance_curve,
        }
        try:
            with TUNING_SETTINGS_PATH.open("w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
        except OSError:
            pass

    def get_tuning(self) -> tuple[float, float, float, float, float, float, list[tuple[float, float]]]:
        """(roll_gain, pitch_gain, tail_threshold_deg, switch_distance_m,
        roll_rate_limit_deg_s, alignment_lead_time_s, roll_distance_curve) —
        поточні значення семи "ползунків" (шість числових + крива), для
        читання з головного циклу (іншого потоку) перед кожним викликом
        PathFollower.update()."""
        return (
            self.roll_gain, self.pitch_gain, self.tail_threshold_deg,
            self.switch_distance_m, self.roll_rate_limit_deg_s, self.alignment_lead_time_s,
            self.roll_distance_curve,
        )

    def _make_waypoint_marker(self, index: int, lat: float, lon: float, status: str):
        circle, outside = _WAYPOINT_COLORS[status]
        return self.map_widget.set_marker(
            lat, lon, text=f"WP{index + 1}",
            marker_color_circle=circle, marker_color_outside=outside,
        )

    def _poll(self) -> None:
        try:
            while True:
                item = self._queue.get_nowait()
                self._apply(*item)
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

    def _draw_arrow(self, current_line, origin_lat: float, origin_lon: float,
                     vector: tuple[float, float], length_m: float, color: str):
        """Малює/оновлює відрізок-стрілку від (origin_lat, origin_lon) на
        length_m метрів у бік vector (east, north), нормалізованого. Повертає
        CanvasPath (новий або той самий, що й current_line, з оновленою
        позицією) — викликач зберігає його назад у відповідний self._..._line."""
        vx, vy = vector
        norm = math.hypot(vx, vy)
        if norm <= 1e-6:
            return current_line
        tip_east = (vx / norm) * length_m
        tip_north = (vy / norm) * length_m
        tip_lat, tip_lon = local_m_to_latlon(tip_east, tip_north, origin_lat, origin_lon)
        points = [(origin_lat, origin_lon), (tip_lat, tip_lon)]
        if current_line is None:
            return self.map_widget.set_path(points, color=color, width=3)
        current_line.set_position_list(points)
        return current_line

    def _apply(
        self,
        frame: Optional[np.ndarray],
        drone_pose: Optional[tuple[float, float, float]],
        real_pose: Optional[tuple[float, float, float]],
        planned_route: Optional[list[list[tuple[float, float]]]],
        current_index: int,
        info_text: str,
        attack_vector: Optional[tuple[float, float]],
        target_vector: Optional[tuple[float, float]],
    ) -> None:
        if frame is not None:
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            self._video_photo = ImageTk.PhotoImage(image=Image.fromarray(rgb))
            self.video_label.configure(image=self._video_photo)

        self.info_label.configure(text=info_text)

        # Кольори міток маршруту відображають прогрес автопілота (PathFollower.
        # reached_count): пройдені — зелені, поточна ціль — червона, решта —
        # сірі. Перестворюємо маркер лише коли статус справді змінився (рідкісна
        # подія, не щотік) — tkintermapview не вміє міняти колір існуючого.
        for i, (lat, lon, _alt) in enumerate(self._waypoints):
            status = "reached" if i < current_index else ("current" if i == current_index else "pending")
            if status != self._wp_status[i]:
                self._wp_status[i] = status
                self._wp_markers[i].delete()
                self._wp_markers[i] = self._make_waypoint_marker(i, lat, lon, status)

        # Запланована Dubins-траєкторія малюється лише РАЗ (вона статична —
        # рахується один раз при отриманні стартової позиції) як набір
        # коротких чорних відрізків із проміжками (імітація пунктиру, бо
        # tkintermapview не підтримує dash напряму).
        if planned_route is not None and not self._planned_route_drawn:
            self._planned_route_drawn = True
            for segment in planned_route:
                self._planned_route_paths.append(
                    self.map_widget.set_path(segment, color=_PLANNED_ROUTE_COLOR, width=2)
                )

        # ТИМЧАСОВО: розраховане (EKF) положення прибрано з карти — лишається
        # лише в текстовій панелі/лозі. Синя лінія/мітка (_path_line/
        # _drone_marker) більше не малюються.

        if real_pose is not None:
            lat, lon, alt = real_pose
            self._append_if_moved(self._real_path_points, lat, lon)

            if len(self._real_path_points) >= 2:
                if self._real_path_line is None:
                    self._real_path_line = self.map_widget.set_path(
                        self._real_path_points, color=_REAL_PATH_COLOR, width=3
                    )
                else:
                    self._real_path_line.set_position_list(self._real_path_points)

            # Зелена мітка — ПОТОЧНА реальна (симуляційна) позиція літака
            # (GeoPositionDroneComponent), з висотою в підписі; сама лінія вище
            # показує лише пройдений шлях, без явної позначки "де він зараз".
            real_text = f"REAL {alt:.0f}m"
            if self._real_marker is None:
                self._real_marker = self.map_widget.set_marker(
                    lat, lon, text=real_text,
                    marker_color_circle=_REAL_COLOR[0], marker_color_outside=_REAL_COLOR[1],
                )
            else:
                self._real_marker.set_position(lat, lon)
                self._real_marker.set_text(real_text)

            # Оранжева стрілка — вектор атаки (напрямок польоту), блакитна —
            # вектор траєкторії, що ЗАРАЗ використовується (до найближчої
            # точки чи вздовж шляху — див. PathFollower.update). Обидві від
            # РЕАЛЬНОЇ позиції (не розрахованої), щоб кут між ними на карті
            # відповідав тому самому куту, що рахує сам автопілот.
            if attack_vector is not None:
                self._attack_vector_line = self._draw_arrow(
                    self._attack_vector_line, lat, lon, attack_vector, ATTACK_VECTOR_LENGTH_M, _ATTACK_VECTOR_COLOR,
                )
            if target_vector is not None:
                self._target_vector_line = self._draw_arrow(
                    self._target_vector_line, lat, lon, target_vector, ATTACK_VECTOR_LENGTH_M, _TARGET_VECTOR_COLOR,
                )

    # ── Публічний API — викликається з головного циклу (іншого потоку) ─────

    def update(
        self,
        frame: Optional[np.ndarray],
        drone_pose: Optional[tuple[float, float, float]],
        real_pose: Optional[tuple[float, float, float]] = None,
        planned_route: Optional[list[list[tuple[float, float]]]] = None,
        current_index: int = 0,
        info_text: str = "",
        attack_vector: Optional[tuple[float, float]] = None,
        target_vector: Optional[tuple[float, float]] = None,
    ) -> None:
        """Неблокуюче: кладе найсвіжіший стан у чергу (розмір 1 — старий стан,
        який вікно не встигло забрати, просто заміняється новим). planned_route
        (пунктирна Dubins-траєкторія) передається лише один раз — головний цикл
        сам стежить, щоб не слати її повторно щокадру. info_text — головна
        інформація (позиція літака/NAV-статус/орієнтири/fps) текстовим блоком
        під відео, а не намальована поверх кадру. attack_vector — (east, north)
        напрямок польоту, малюється на карті оранжевою стрілкою від real_pose;
        target_vector — (east, north) вектор траєкторії, що ЗАРАЗ використовує
        автопілот (до найближчої точки чи вздовж шляху), малюється блакитною
        стрілкою від real_pose."""
        item = (
            frame, drone_pose, real_pose, planned_route, current_index, info_text, attack_vector, target_vector,
        )
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

EnvelopeResult = tuple[Optional[np.ndarray], Optional[list[Landmark]], Optional[tuple[float, float, float]]]


def process_envelope(parts: list[bytes]) -> EnvelopeResult:
    """Розбирає мультипарт-повідомлення шини. Повертає (кадр камери, орієнтири, реальна
    geo-позиція дрона) — кожне або None, якщо відповідний топік не прийшов у цьому повідомленні."""
    try:
        envelope = json.loads(parts[0].decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None, None, None

    frame: Optional[np.ndarray] = None
    landmarks: Optional[list[Landmark]] = None
    real_pose: Optional[tuple[float, float, float]] = None

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

    return frame, landmarks, real_pose


# ── Візуалізація одного кадру ───────────────────────────────────────────────

def draw_and_report(
    frame: np.ndarray,
    matches: list[Match],
    landmarks_visible: int,
    drone_pose: Optional[tuple[float, float, float]],
    seconds_since_fix: Optional[float],
    nav_status: Optional[str],
    class_names: dict[int, str],
    video_fps: float,
    yolo_latency_ms: Optional[float],
) -> tuple[np.ndarray, str]:
    """Повертає (кадр з розміткою об'єктів, текстовий блок з головною
    інформацією). Розмітка бокс/мітка/гео-текст під кожним об'єктом лишається
    НА кадрі (прив'язана до конкретного місця в зображенні); загальний статус
    (позиція літака, NAV, к-сть орієнтирів, fps) — у ТЕКСТОВОМУ БЛОЦІ поряд
    (AppWindow, окрема панель під відео), а не намальований поверх картинки."""
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

    # ── Позиція літака (EKF: PnP-фікси, злиті з предбаченням фізичної моделі) ─
    # Раніше цей і наступні рядки МАЛЮВАЛИСЬ на кадрі (cv2.putText); тепер це
    # окремий текстовий блок (info_lines), який показується в AppWindow під
    # відео, а не поверх картинки.
    info_lines: list[str] = []
    if drone_pose is not None:
        lat, lon, alt = drone_pose
        have_recent_fix = seconds_since_fix is not None and seconds_since_fix < MAX_FIX_LOSS_S
        if have_recent_fix:
            fix_text = f"measured, {n_matched} pts"
        else:
            loss_text = "n/a" if seconds_since_fix is None or math.isinf(seconds_since_fix) else f"{seconds_since_fix:.1f}s"
            fix_text = f"DEAD-RECKONING, no fix {loss_text}"
        drone_text = f"DRONE (EKF): lat={lat:.6f} lon={lon:.6f} alt={alt:.1f} m  ({fix_text})"
        print(f"[DRONE] lat={lat:.6f}  lon={lon:.6f}  alt={alt:.1f}  ({fix_text})")
    else:
        drone_text = "DRONE (EKF): not initialized yet — waiting for starting position"
    info_lines.append(drone_text)

    if nav_status is not None:
        info_lines.append(f"NAV: {nav_status}")

    info_lines.append(f"landmarks visible: {landmarks_visible}")

    if yolo_latency_ms is not None:
        info_lines.append(
            f"video: {video_fps:5.1f} fps   yolo: {yolo_latency_ms:6.1f} ms ({1000.0 / yolo_latency_ms:4.1f} fps)"
        )
    else:
        info_lines.append(f"video: {video_fps:5.1f} fps   yolo: waiting for first result...")

    return vis, "\n".join(info_lines)


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

    window = AppWindow(WAYPOINTS)
    print("Вікно відкрито (відео + карта в одному Tkinter-вікні).")

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    cmd_log_path = LOG_DIR / f"nav_commands_{time.strftime('%Y%m%d_%H%M%S')}.csv"
    cmd_log_file = cmd_log_path.open("w", newline="", encoding="utf-8")
    cmd_log_writer = csv.DictWriter(cmd_log_file, fieldnames=[
        "t_s", "have_fix", "seconds_since_fix", "reached_count",
        "landmarks_visible", "landmarks_matched", "pnp_ok", "ekf_update_accepted",
        "drone_lat", "drone_lon", "drone_alt_m", "speed_mps", "speed_kmh",
        "roll_cmd_deg", "pitch_cmd_deg", "yaw_rate_cmd_deg_s", "thrust_cmd",
        "dist_to_end_m", "heading_error_deg", "status",
    ])
    cmd_log_writer.writeheader()
    print(f"Лог команд керування: {cmd_log_path}")
    t_start = time.perf_counter()

    ekf = CessnaEKF()
    path_follower: Optional[PathFollower] = None

    latest_landmarks: list[Landmark] = []
    # Опорна точка для локальної ENU-площини — фіксується один раз, по першому
    # отриманому списку орієнтирів, і більше не змінюється, щоб PnP-оцінки
    # різних кадрів лишались у одній системі координат.
    ref_lat: Optional[float] = None
    ref_lon: Optional[float] = None
    # Реальна (симуляційна) позиція дрона з GeoPositionDroneComponent — лише для
    # порівняння з нашою PnP-оцінкою на карті (зелена лінія).
    latest_real_pose: Optional[tuple[float, float, float]] = None
    # ТИМЧАСОВО: для attack_vector, розрахованого з drone_geo_position (див.
    # нижче) — попередня реальна локальна позиція й останній дійсний напрямок.
    last_real_local: Optional[tuple[float, float]] = None
    last_attack_vector: tuple[float, float] = (1.0, 0.0)

    # Запланована Dubins-траєкторія (старт -> усі WAYPOINTS) для Cessna 172N —
    # рахується ОДИН РАЗ, щойно відома стартова позиція (власна PnP-оцінка,
    # якщо вже є фікс, інакше — реальна позиція з drone_geo_position) і опорна
    # точка ENU. Той самий момент ініціалізує EKF і PathFollower (автопілот).
    planned_route_dashes: Optional[list[list[tuple[float, float]]]] = None

    video_fps = 0.0
    last_frame_time = time.perf_counter()

    print(f"Підключено до {ZMQ_ENDPOINT}. Команди керування -> {CMD_ENDPOINT}. "
          f"Очікування кадрів з камери... ('q' у вікні або закриття вікна — вихід)")

    profile: Optional[RouteProfile] = None

    try:
        while not window.quit_event.is_set():
            if window.restart_event.is_set():
                window.restart_event.clear()
                # Скидаємо ЛИШЕ стан місії/автопілота (кнопка "Рестарт місії",
                # AppWindow._on_restart_click вже скинула малюнок на карті) —
                # ref_lat/ref_lon (ENU-опорна точка), ZMQ/YOLO-з'єднання й
                # latest_landmarks/latest_real_pose НЕ чіпаємо: вони прив'язані
                # до фізичного світу й сенсорів, а не до прогресу місії.
                ekf = CessnaEKF()
                path_follower = None
                profile = None
                planned_route_dashes = None
                last_real_local = None
                last_attack_vector = (1.0, 0.0)
                print("Місію перезапущено: EKF/автопілот/траєкторія скинуто, "
                      "старт відбудеться заново з наступного доступного фіксу.")

            if not socket.poll(timeout=1000):
                continue

            parts = socket.recv_multipart()
            if len(parts) < 2:
                continue

            frame, landmarks, real_pose = process_envelope(parts)
            if landmarks is not None:
                latest_landmarks = landmarks
                if ref_lat is None and landmarks:
                    ref_lat = sum(lm.latitude for lm in landmarks) / len(landmarks)
                    ref_lon = sum(lm.longitude for lm in landmarks) / len(landmarks)
                    print(f"Опорна точка ENU зафіксована: lat={ref_lat:.6f} lon={ref_lon:.6f}")
            if real_pose is not None:
                latest_real_pose = real_pose

            if frame is None:
                continue

            now = time.perf_counter()
            dt = now - last_frame_time
            last_frame_time = now
            if dt > 0:
                instant_fps = 1.0 / dt
                video_fps = instant_fps if video_fps == 0.0 else video_fps * 0.9 + instant_fps * 0.1

            matches = pipeline.tick(frame, latest_landmarks)
            landmarks_matched = sum(1 for m in matches if m[5] is not None)  # для діагностики втрати фіксу в лозі

            # EKF-передбачення рухається ВПЕРЕД щотік (dead reckoning, керована
            # ОСТАННЬОЮ виданою командою) — незалежно від того, чи YOLO встигло
            # дати новий результат цього тіку, чи орієнтири взагалі видно.
            if ekf.initialized and dt > 0:
                ekf.predict(dt)

            raw_pose: Optional[tuple[float, float, float]] = None
            ekf_update_accepted: Optional[bool] = None  # None, доки PnP не дав фікс цього тіку — нема що приймати/відкидати
            if ref_lat is not None:
                raw_pose = estimate_drone_position(matches, ref_lat, ref_lon)
                if raw_pose is not None and ekf.initialized:
                    raw_east, raw_north = latlon_to_local_m(raw_pose[0], raw_pose[1], ref_lat, ref_lon)
                    ekf_update_accepted = ekf.update((raw_east, raw_north, raw_pose[2]), now)

            # Ініціалізація EKF + автопілота + запланованої траєкторії — ОДИН
            # РАЗ, щойно відома стартова позиція (власна PnP-оцінка, якщо вже є
            # фікс, інакше — реальна позиція з drone_geo_position) і опорна
            # точка ENU.
            if not ekf.initialized and ref_lat is not None and WAYPOINTS:
                start_pose = raw_pose if raw_pose is not None else latest_real_pose
                if start_pose is not None:
                    start_lat, start_lon, start_alt = start_pose
                    route_latlon = compute_dubins_route(
                        (start_lat, start_lon), WAYPOINTS, ref_lat, ref_lon, DUBINS_TURN_RADIUS_M,
                    )
                    route_local = [latlon_to_local_m(lat, lon, ref_lat, ref_lon) for lat, lon in route_latlon]
                    planned_route_dashes = split_into_dash_segments(route_latlon, route_local)

                    profile = compute_dubins_route_profile(
                        start_pose, WAYPOINTS, ref_lat, ref_lon, DUBINS_TURN_RADIUS_M,
                    )
                    path_follower = PathFollower()

                    start_east, start_north = profile.local[0]
                    if len(profile.local) >= 2:
                        init_yaw = math.atan2(
                            profile.local[1][1] - profile.local[0][1], profile.local[1][0] - profile.local[0][0],
                        )
                    else:
                        init_yaw = 0.0
                    ekf.initialize(start_east, start_north, start_alt, init_yaw, CESSNA172N_CRUISE_SPEED_MPS, now)

                    print(f"Автопілот увімкнено: старт=({start_lat:.6f}, {start_lon:.6f}), "
                          f"радіус віражу={DUBINS_TURN_RADIUS_M:.0f} м "
                          f"(Cessna 172N: V={CESSNA172N_CRUISE_SPEED_MPS:.0f} м/с, "
                          f"крен={CESSNA172N_BANK_DEG:.0f}°), точок маршруту={len(WAYPOINTS)}, "
                          f"команди -> {CMD_ENDPOINT}")

            nav_cmd: Optional[NavCommand] = None
            drone_pose: Optional[tuple[float, float, float]] = None
            seconds_since_fix: Optional[float] = None
            attack_vector: Optional[tuple[float, float]] = None
            if ekf.initialized and path_follower is not None:
                east, north, alt = ekf.pose_local
                drone_pose = local_m_to_latlon(east, north, ref_lat, ref_lon) + (alt,)
                seconds_since_fix = ekf.seconds_since_measurement(now)

                # ТИМЧАСОВО (для розробки/тестування алгоритму): у сам автопілот
                # передаємо РЕАЛЬНУ (симуляційну) позицію з drone_geo_position
                # замість EKF-оцінки — щоб перевіряти логіку без шуму/похибок
                # PnP-фіксу. attack_vector теж рахується з неї — напрямок
                # зміщення між двома послідовними реальними фіксами (курс EKF
                # тут НЕ використовується). Приберіть цей блок і поверніть
                # (east, north, alt) / ekf.yaw_rad, коли автопілот працюватиме
                # на власній оцінці позиції.
                if latest_real_pose is not None:
                    real_east, real_north = latlon_to_local_m(
                        latest_real_pose[0], latest_real_pose[1], ref_lat, ref_lon,
                    )
                    position_for_autopilot = (real_east, real_north, latest_real_pose[2])

                    if last_real_local is not None:
                        dx = real_east - last_real_local[0]
                        dy = real_north - last_real_local[1]
                        disp = math.hypot(dx, dy)
                        if disp >= 0.5:  # мінімальне зміщення, щоб не смикати напрямок на шумі між фіксами
                            last_attack_vector = (dx / disp, dy / disp)
                    last_real_local = (real_east, real_north)
                    attack_vector = last_attack_vector
                else:
                    position_for_autopilot = (east, north, alt)
                    attack_vector = (math.cos(ekf.yaw_rad), math.sin(ekf.yaw_rad))

                (
                    roll_gain, pitch_gain, tail_threshold_deg, switch_distance_m, roll_rate_limit_deg_s,
                    alignment_lead_time_s, roll_distance_curve,
                ) = window.get_tuning()
                nav_cmd = path_follower.update(
                    profile, position_for_autopilot, ekf.yaw_rad, ekf.speed_mps, attack_vector,
                    roll_gain, pitch_gain, tail_threshold_deg, switch_distance_m, roll_rate_limit_deg_s,
                    alignment_lead_time_s, roll_distance_curve, now,
                )
                ekf.set_control(nav_cmd.roll_deg, nav_cmd.pitch_deg, nav_cmd.yaw_rate_deg_s, nav_cmd.thrust)

                try:
                    cmd_socket.send_json(
                        build_command(nav_cmd.roll_deg, nav_cmd.pitch_deg, nav_cmd.yaw_rate_deg_s, nav_cmd.thrust),
                        flags=zmq.NOBLOCK,
                    )
                except zmq.Again:
                    pass

                print(f"[CMD] roll={nav_cmd.roll_deg:+6.1f} pitch={nav_cmd.pitch_deg:+6.1f} "
                      f"yaw_rate={nav_cmd.yaw_rate_deg_s:+5.1f} thrust={nav_cmd.thrust:.2f}  "
                      f"| {nav_cmd.status}")

                cmd_log_writer.writerow({
                    "t_s": f"{now - t_start:.3f}", "have_fix": int(seconds_since_fix < MAX_FIX_LOSS_S),
                    "seconds_since_fix": "" if math.isinf(seconds_since_fix) else f"{seconds_since_fix:.3f}",
                    "reached_count": nav_cmd.reached_count,
                    "landmarks_visible": len(latest_landmarks), "landmarks_matched": landmarks_matched,
                    "pnp_ok": int(raw_pose is not None),
                    "ekf_update_accepted": "" if ekf_update_accepted is None else int(ekf_update_accepted),
                    "drone_lat": drone_pose[0], "drone_lon": drone_pose[1], "drone_alt_m": drone_pose[2],
                    "speed_mps": f"{ekf.speed_mps:.2f}", "speed_kmh": f"{ekf.speed_mps * 3.6:.1f}",
                    "roll_cmd_deg": f"{nav_cmd.roll_deg:.3f}", "pitch_cmd_deg": f"{nav_cmd.pitch_deg:.3f}",
                    "yaw_rate_cmd_deg_s": f"{nav_cmd.yaw_rate_deg_s:.3f}", "thrust_cmd": f"{nav_cmd.thrust:.3f}",
                    "dist_to_end_m": nav_cmd.dist_to_end_m, "heading_error_deg": nav_cmd.heading_error_deg,
                    "status": nav_cmd.status,
                })
                cmd_log_file.flush()

            vis, info_text = draw_and_report(
                frame, matches, len(latest_landmarks), drone_pose, seconds_since_fix,
                nav_cmd.status if nav_cmd is not None else None,
                class_names, video_fps, pipeline.last_latency_ms,
            )

            current_index = path_follower.reached_count if path_follower is not None else 0
            target_vector = nav_cmd.target_vector if nav_cmd is not None else None
            window.update(
                vis, drone_pose, latest_real_pose, planned_route_dashes, current_index, info_text,
                attack_vector, target_vector,
            )

    except KeyboardInterrupt:
        print("Зупинено користувачем.")
    finally:
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
        cmd_log_file.close()
        window.close()


if __name__ == "__main__":
    main()
