"""
Тренування YOLO-детектора на датасеті, зібраному collect_custom_objects_dataset.py:
images/train/*.jpg + labels/train/*.txt (клас — з поля "type" кожного об'єкта; id/bbox_px/
lat-long — у labels/train/*.meta.json поруч, для тренування не використовується).

Запуск: `python train_yolo.py` (з будь-якої директорії — шлях до data.yaml резолвиться
відносно цього файлу, так само як шлях val/train у самому data.yaml, який зберігає абсолютний
`path:`).
"""

import os
from ultralytics import YOLO

DATA_YAML = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data.yaml")

if __name__ == '__main__':
    # Базова модель — Nano, найшвидша, для старту цього достатньо.
    # Якщо точності не вистачить і залізо дозволяє — 'yolov8s.pt' / 'yolov8m.pt'.
    model = YOLO('yolov8n.pt')

    results = model.train(
        data=DATA_YAML,
        epochs=100,             # Якщо прогрес зупиниться раніше — patience завершить тренування сам.
        imgsz=640,              # Відповідає роздільній здатності онбордової камери (640x480).
        batch=8,                # Зменш до 4/2, якщо відеокарті не вистачає VRAM.
        device='',              # '' — автовибір GPU. 'cpu', якщо GPU немає (буде значно довше).
        workers=2,              # Кількість потоків завантаження даних — зменш, якщо CPU слабкий.
        project='runs',         # Результати запуску -> runs/custom_objects/<n>/weights/best.pt
        name='custom_objects',
        patience=20,            # Рання зупинка, якщо 20 епох поспіль без прогресу.
    )

    print(f"Тренування завершено. Ваги — у {results.save_dir / 'weights' / 'best.pt'}")

    # data.yaml зараз задає val: як ту саму train-директорію (окремого хелд-аут спліту дане
    # зібрання ще не робить) — тож метрики валідації нижче не показові, лише службовий прогрес.
