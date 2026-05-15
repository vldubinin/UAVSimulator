# Edge AI Система наведення та оцінки дистанції

Система реального часу для виявлення, трекінгу та оцінки відстані до дрона за допомогою моно-камери. Побудована за архітектурою **Dual-Loop Asynchronous** для стабільного FPS на слабкому залізі (Raspberry Pi).

---

## Архітектура

```
┌─────────────────────────────────────────────┐   ┌──────────────────────────┐
│           Процес А  (main.py)               │   │  Процес Б                │
│                                             │   │  (classification_worker) │
│  ZMQ Camera Bus                             │   │                          │
│       ↓                                     │   │  Приймає crop кадр       │
│  camera_tick_processor.tick(frame)          │   │  ↓                       │
│       │                                     │   │  Визначає тип дрона      │
│       ├─ SEARCHING/LOST → detector.py       │   │  ↓                       │
│       ├─ TRACKING       → tracker.py        │   │  Повертає size_mm + Q    │
│       ├─ Distance       → distance_estimator│   │                          │
│       └─ Кожні N кадрів → [crop] ──────────┼──►│  (порт 5600)             │
│                           [result] ◄────────┼───│  (порт 5601)             │
└─────────────────────────────────────────────┘   └──────────────────────────┘
```

**TargetState** — глобальний стан цілі: `SEARCHING → TRACKING → LOST`

---

## Вимоги

- Python **3.10+**
- `opencv-contrib-python` (не `opencv-python` — потрібні трекери з contrib)
- Натренована модель YOLOv8 у форматі `.pt`
- ZMQ-шина з камери (або інше джерело кадрів)

---

## Встановлення

### 1. Клонувати / розпакувати проєкт

```bash
cd navigation
```

### 2. Створити та активувати virtual environment (рекомендовано)

```bash
python -m venv venv

# Windows
venv\Scripts\activate

# Linux / macOS
source venv/bin/activate
```

### 3. Встановити залежності

> ⚠️ Якщо раніше встановлювали `opencv-python` — спочатку видаліть його:
> ```bash
> pip uninstall opencv-python -y
> ```

```bash
pip install -r requirements.txt
```

### 4. Перевірити OpenCV

```bash
python -c "import cv2; print(cv2.__version__); print(cv2.legacy.TrackerCSRT_create())"
```

Очікуваний вивід: версія OpenCV і `<TrackerCSRT ...>`.

---

## Конфігурація

### Шлях до моделі — `main.py`, рядок 20

```python
MODEL_PATH: str = r"C:\шлях\до\вашої\моделі\best.pt"
```

### Фокусна відстань камери — `constants.py`, рядок 3

```python
FOCAL_LENGTH_PX: float = 1000.0  # замініть на реальне значення вашої камери
```

Для калібрування: сфотографуйте об'єкт відомого розміру на відомій відстані та обчисліть:
```
focal_length = (pixel_width * known_distance_mm) / known_size_mm
```

### Адреса ZMQ камери — `main.py`, рядок 22

```python
CAMERA_ZMQ_ADDRESS: str = "tcp://127.0.0.1:5555"
```

### Відображення відео — `main.py`, рядок 23

```python
SHOW_VIDEO: bool = True   # False для headless (без вікна, напр. Raspberry Pi)
```

---

## Запуск

Система складається з двох процесів. Запускайте в **двох окремих терміналах**.

### Термінал 1 — Фоновий класифікатор (Процес Б)

```bash
python classification_worker.py
```

Очікуваний вивід:
```
20:00:00 [WORKER] INFO  PULL socket bound on tcp://*:5600
20:00:00 [WORKER] INFO  Worker ready  [TEST MODE — always classifies as CESSNA_172]
```

> **Примітка:** зараз воркер завжди повертає клас `CESSNA_172` (тестовий режим).
> Коли буде натренована мережа класифікації — замініть тіло функції `_classify()` у `classification_worker.py`.

### Термінал 2 — Головна система (Процес А)

```bash
python main.py
```

Очікуваний вивід:
```
20:00:01 [MAIN] INFO  All components initialised. Waiting for camera stream...
20:00:05 [MAIN] INFO  Target acquired bbox=(x, y, w, h)  [SEARCHING → TRACKING]
20:00:05 [MAIN] INFO  Q-factor updated for target class CESSNA_172  (size=11000 mm, Q=0.10)
```

### Зупинка

`Ctrl+C` у будь-якому терміналі. Або натисніть `Q` у вікні відео.

---

## Структура файлів

```
navigation/
│
├── main.py                   # Точка входу. Тут задається MODEL_PATH
├── camera_tick_processor.py  # Ядро: State Machine + DI-оркестратор
│
├── detector.py               # YOLOv8 детектор (пошук цілі)
├── tracker.py                # CSRT трекер (ведення цілі між кадрами)
├── distance_estimator.py     # Монокулярна дистанція + фільтр Калмана
│
├── ipc_communicator.py       # ZMQ PUSH/PULL обгортка (порти 5600/5601)
├── classification_worker.py  # Процес Б: класифікація типу дрона
│
├── state.py                  # TargetState dataclass + TargetStatus enum
├── constants.py              # KNOWN_DRONES_DB, фокусна відстань, IPC параметри
│
└── requirements.txt
```

---

## Словник дронів (`constants.py`)

| Клас | Розмір (мм) | Kalman Q | Опис |
|---|---|---|---|
| `DEFAULT_AVERAGE_DRONE` | 350 | 5.0 | Дефолт при старті |
| `MAVIC_3` | 347 | 2.0 | Плавний рух |
| `FPV_7_INCH` | 320 | 15.0 | Різкі маневри |
| `HEAVY_BOMBER` | 800 | 0.5 | Інертний |
| `CESSNA_172` | 11000 | 0.1 | Еталон для тестування |

Для додавання нового класу — додайте запис у `KNOWN_DRONES_DB` і натренуйте класифікатор.

---

## Порти ZMQ

| Порт | Напрямок | Призначення |
|---|---|---|
| `5555` | Зовнішній → Процес А | ZMQ-шина камери (вхідні кадри) |
| `5600` | Процес А → Процес Б | Відправка crop-кадрів класифікатору |
| `5601` | Процес Б → Процес А | Результати класифікації (тип дрона) |
