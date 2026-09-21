"""
TrajectoryMap — нативне Tkinter-вікно (tkintermapview) з картою OpenStreetMap,
що малює дві траєкторії дрона наживо, без API-ключа й без локального
HTTP-сервера/браузера:
  - синя лінія + синя мітка  — ВИЗНАЧЕНА (розрахована з відео через PnP) позиція;
  - зелена лінія + зелена мітка — ФАКТИЧНА (реальна, симуляційна) позиція.

Без автопілота й без маршрутних точок — це лише спостереження: наскільки
розрахована траєкторія збігається з фактичною.

Один публічний метод — update(estimated_position, actual_position) — можна
викликати з будь-якою з двох позицій відсутньою (None), якщо фіксу зараз
немає.

Tkinter не потокобезпечний для довільних викликів із чужого потоку, тож усе
вікно (root, map_widget, маркери) живе й будується ЛИШЕ у власному фоновому
потоці; публічний update() лише кладе новий стан у чергу (queue.Queue).
Потребує інтернету (тайли OSM), без API-ключа. Встановлення: `pip install tkintermapview`.
"""

from __future__ import annotations

import math
import queue
import threading
import tkinter as tk
from typing import Optional

try:
    from tkintermapview import TkinterMapView
except ImportError:
    TkinterMapView = None

from common import Position, latlon_to_local_m

_ESTIMATED_COLOR = ("#1e88e5", "#0d47a1")  # синя лінія — визначена (PnP) позиція
_ACTUAL_COLOR = ("#00c853", "#1b5e20")     # зелена лінія — фактична позиція

MAP_ZOOM: int = 15
MIN_PATH_POINT_SPACING_M: float = 1.0  # не додавати нову точку треку, поки дрон не відійшов на стільки


class TrajectoryMap:
    """Малює на карті дві живі траєкторії — визначену (PnP) і фактичну.
    Не прив'язаний до конкретного сценарію польоту (без маршрутних точок і
    без керування) — придатний для повторного використання в інших тестах
    оцінки траєкторії."""

    POLL_MS = 300

    def __init__(self) -> None:
        if TkinterMapView is None:
            raise RuntimeError("tkintermapview не встановлено: pip install tkintermapview")

        QueueItem = tuple[Optional[Position], Optional[Position]]
        self._queue: "queue.Queue[QueueItem]" = queue.Queue(maxsize=1)
        self._running = True
        self._ready = threading.Event()

        self._first_position_seen = False

        self._estimated_path_points: list[tuple[float, float]] = []
        self._estimated_path_line = None
        self._estimated_marker = None

        self._actual_path_points: list[tuple[float, float]] = []
        self._actual_path_line = None
        self._actual_marker = None

        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=10.0)

    # ── Публічний API — викликається з головного циклу (іншого потоку) ─────

    def update(
        self,
        estimated_position: Optional[Position],
        actual_position: Optional[Position],
    ) -> None:
        """Неблокуюче: кладе найсвіжіший стан у чергу (розмір 1 — старий стан,
        який вікно не встигло забрати, просто заміняється новим)."""
        item = (estimated_position, actual_position)
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

    # ── Фоновий потік вікна — усе, що торкається Tkinter, лише тут ──────────

    def _run(self) -> None:
        self.root = tk.Tk()
        self.root.title("Trajectory estimation: calculated vs actual")
        self.root.geometry("900x700")
        # Закриття хрестиком не має валити основний цикл — просто ігноруємо клік,
        # вікно завершиться разом зі скриптом (daemon-потік).
        self.root.protocol("WM_DELETE_WINDOW", lambda: None)

        self.map_widget = TkinterMapView(self.root, corner_radius=0)
        self.map_widget.pack(fill="both", expand=True)
        self.map_widget.set_zoom(MAP_ZOOM)

        self.root.after(self.POLL_MS, self._poll)
        self._ready.set()
        self.root.mainloop()

    def _poll(self) -> None:
        try:
            while True:
                estimated_position, actual_position = self._queue.get_nowait()
                self._apply(estimated_position, actual_position)
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
        estimated_position: Optional[Position],
        actual_position: Optional[Position],
    ) -> None:
        if not self._first_position_seen:
            first = estimated_position or actual_position
            if first is not None:
                self.map_widget.set_position(first[0], first[1])
                self._first_position_seen = True

        if estimated_position is not None:
            lat, lon, _alt = estimated_position
            self._append_if_moved(self._estimated_path_points, lat, lon)

            if len(self._estimated_path_points) >= 2:
                if self._estimated_path_line is None:
                    self._estimated_path_line = self.map_widget.set_path(
                        self._estimated_path_points, color=_ESTIMATED_COLOR[0], width=3
                    )
                else:
                    self._estimated_path_line.set_position_list(self._estimated_path_points)

            if self._estimated_marker is None:
                self._estimated_marker = self.map_widget.set_marker(
                    lat, lon, text="визначена",
                    marker_color_circle=_ESTIMATED_COLOR[0], marker_color_outside=_ESTIMATED_COLOR[1],
                )
            else:
                self._estimated_marker.set_position(lat, lon)

        if actual_position is not None:
            lat, lon, _alt = actual_position
            self._append_if_moved(self._actual_path_points, lat, lon)

            if len(self._actual_path_points) >= 2:
                if self._actual_path_line is None:
                    self._actual_path_line = self.map_widget.set_path(
                        self._actual_path_points, color=_ACTUAL_COLOR[0], width=3
                    )
                else:
                    self._actual_path_line.set_position_list(self._actual_path_points)

            if self._actual_marker is None:
                self._actual_marker = self.map_widget.set_marker(
                    lat, lon, text="фактична",
                    marker_color_circle=_ACTUAL_COLOR[0], marker_color_outside=_ACTUAL_COLOR[1],
                )
            else:
                self._actual_marker.set_position(lat, lon)
