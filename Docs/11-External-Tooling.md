# 11 — Зовнішні Python-інструменти

Два незалежні сімейства зовнішніх Python-скриптів, жодне не частина збірки UE.

**`Tools/TestingPlatform/`** — окремі процеси, що спілкуються з симулятором
**лише** через ZeroMQ: сенсорна шина PUB `tcp://*:5555`, приймач команд
атитюду PULL `tcp://*:5556`.

**`Tools/ProjectTools/`** — навпаки, без жодного ZMQ: `configurate_env_actors.py`
спілкується з Unreal виключно через файл `env_actors.json`, синхронно (запускає
його `AerodynamicToolRunner::RunPythonScript`, блокуючи гру, доки вікно карти не
закриють) — див. окремий розділ унизу файлу.

## `attitude_control/` — керування автопілотом ззовні

Усі шлють `SET_ATTITUDE_TARGET` по ZMQ PUSH → `tcp://127.0.0.1:5556` (див.
[03-PilotInput-and-Autopilot.md](03-PilotInput-and-Autopilot.md#zmq-протокол-уставок-атитюду)).
Керма приймає **лише абсолютні кути** roll/pitch (світова СК), швидкість рискання
й тягу — курсу/точки в протоколі немає.

| Файл | Призначення |
|------|-------------|
| `circle_autopilot.py` | «Зроби коло»: тримає сталий крен (літак сам іде у віраж), тангаж тримає висоту одноконтурним PID по топіку `drone_position`, рискання = 0. Фази `SETTLE → TURN → RECOVER`. Пише `logs/circle_*.csv`, наприкінці — МНК-апроксимація кола (центр, радіус, RMS). Потребує режиму `Playback and Auto Track` + сенсор `Position` |
| `maneuvers.py` | Виконавець маневрів: `s_turns`, `figure_eight`, `wing_rock`, `climb_turn`, `spiral_descent`. Той самий канал + телеметрія `drone_position`, CSV-лог `logs/maneuvers_*` |
| `keyboard_attitude_test_client.py` | Ручний тест-клієнт: Numpad 8/2/1/3/7/9/+/−/0 (утримання) → безперервні `SET_ATTITUDE_TARGET`. Навмисно Numpad (не WSAD/стрілки — вони прив'язані до ручного керування) |
| `debug_numpad_keys.py` | Діагностика: які клавіші Numpad реально реагують |
| `marker/map_object_marker.py` | Інтерактивний Google Maps canvas для розмітки об'єктів (building/road/bridge/…). Клік 4 кутів або drag прямокутника → JSON-запис у `map_objects.json` (схема `{elementId, type, bbox:{x_min,x_max,y_min,y_max}, altitude}` — та сама, що `UCustomSurroundingsScannerComponent::ObjectsJson` і `AYoloMarkerDatasetActor`). Список об'єктів праворуч, drag-редагування кутів, видалення |
| `marker/map_objects.json` | Файл-результат розмітки (джерело маркерів за замовч. для `AYoloMarkerDatasetActor`) |

## `navigation/` — наведення на слабкому залізі

Dual-Loop асинхронна система реального часу для виявлення / трекінгу / оцінки
відстані до дрона по моно-камері (розрахована на Raspberry Pi). **Має власний
`CLAUDE.md` і `README.md`** — детальний опис там.

- **Процес A** (`main.py`) — реальночасовий цикл: ZMQ SUB `tcp://127.0.0.1:5555`,
  `CameraTickProcessor.tick(frame)` (стейт-машина `SEARCHING → TRACKING → LOST`),
  YOLOv8 детектор (`detector.py`) + CSRT трекер (`tracker.py`) + монокулярна
  дистанція з Kalman-фільтром (`distance_estimator.py`). < 30 мс/кадр.
- **Процес B** (`classification_worker.py`) — фонова класифікація типу дрона
  через PUSH/PULL порти 5600/5601. Зараз **stub** — завжди `CESSNA_172`.
- Формат камери в payload: `[4-байт LE int: довжина JSON][JSON metadata][JPEG]`.
- Lidar payload: `{"ActorName": distance_cm, ...}`, Процес A бере `min(values)/100`.
- `constants.py` — `KNOWN_DRONES_DB` (size_mm + Kalman Q на клас),
  `FOCAL_LENGTH_PX` (320 для 640×480 HFOV 90°).

## `positioning/` — запис/візуалізація датасетів позиціонування

| Файл | Призначення |
|------|-------------|
| `record_cesium_objects.py` | Приймає `camera` (JPEG) + `cesium_objects` з тієї самої шини, пише `frames/frame_*.jpg`, `landmarks.csv` (id,x=lat,y=lon,z=alt — first-seen, лише росте), `observations.csv` (frame,landmark_id,pixel_x,pixel_y,visible). Спостереження записуються лише коли `cesium_objects` прийшов у тому ж bus-повідомленні, що й `camera`. `SKIP_FIRST_FRAMES` — прогрів. Резюмиться між запусками |
| `visualize_cesium_objects.py` | Інтерактивний перегляд: накладає детекції `cesium_objects` на записані кадри (маркер у `(pixel_x, pixel_y)`, підпис id/lat/lon/vis/alt; текст підпису темпорально згладжений per-landmark, маркер — сира позиція) |
| `export_cesium_objects_video.py` / `_plain.py` | Експорт відео з накладеними детекціями |

## `training/` — навчання YOLO

| Файл | Призначення |
|------|-------------|
| `collect_dataset.py` | ZMQ SUB `tcp://127.0.0.1:5555`, зберігає кадри + bbox у YOLO-форматі (топік `bbox`) |
| `train_yolo.py` | `ultralytics` YOLO: `yolov8n.pt`, `data.yaml`, 100 епох, `imgsz=640`, ваги → `runs/detect/train/weights/best.pt` |
| `data.yaml` | Конфіг датасету для тренування |

## `Tools/ProjectTools/` — інструмент карти (без ZMQ)

| Файл | Призначення |
|------|-------------|
| `configurate_env_actors.py` | Tkinter-вікно з інтерактивною картою (`tkintermapview`) для розміщення зон РЕБ і вітрових векторів. Запускається виключно з `AEnvironmentActorManager::OpenConfigurationTool()`; читає/пише `env_actors.json` поруч із собою. Детально — `13-Environment-Actors.md` |
| `env_actors.json` | Персистентний стан — масиви `"electronic_warfare"` і `"wind"`, синхронізовані двобічно з `AEnvironmentActorManager::EWConfigurations`/`WindConfigurations` |

## Зведення ZMQ-портів

| Порт | Напрямок | Призначення |
|------|----------|-------------|
| `5555` | Симулятор (PUB) → інструменти (SUB) | Сенсорна шина (`USensorBusComponent::Endpoint`) |
| `5556` | Інструменти (PUSH) → симулятор (PULL) | Уставки атитюду (`UAttitudeControlComponent::CommandEndpoint` / `GameMode::AttitudeCommandEndpoint`) |
| `5600` | `navigation` Процес A (PUSH) → Процес B (PULL) | Crop-кадри на класифікацію |
| `5601` | `navigation` Процес B (PUSH) → Процес A (PULL) | Результати класифікації |
