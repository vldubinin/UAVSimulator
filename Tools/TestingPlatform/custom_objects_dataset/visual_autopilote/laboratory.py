"""
Laboratory — нативне Tkinter-вікно (tkintermapview) з картою OpenStreetMap для
звірки розрахованої (PnP) позиції дрона з фактичною, без API-ключа й без
локального HTTP-сервера/браузера. Мітки маршрутних точок — трьох кольорів
(сіра/очікує, червона/поточна ціль, зелена/досягнуто); синя лінія + синя мітка
— розрахована позиція; зелена лінія — фактична позиція.

Один публічний метод — update(calculated_position, actual_position, targets,
current_target) — не прив'язаний до конкретного сценарію навігації (маршрутні
точки передаються щоразу разом з даними, а не задаються наперед у
конструкторі), тож клас придатний для повторного використання в інших тестах.

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

# Кольори маркера точки маршруту залежно від статусу: (заливка кола, обвід).
_WAYPOINT_COLORS = {
    "pending": ("#9e9e9e", "#616161"),
    "current": ("#e53935", "#b71c1c"),
    "reached": ("#43a047", "#2e7d32"),
}
_DRONE_COLOR = ("#1e88e5", "#0d47a1")   # синя лінія — розрахована (PnP) позиція
_REAL_PATH_COLOR = "#00c853"            # зелена лінія — фактична позиція, лише для звірки на око

MAP_ZOOM: int = 15
MIN_PATH_POINT_SPACING_M: float = 1.0   # не додавати нову точку треку, поки дрон не відійшов на стільки


class Laboratory:
    """Відображає інформацію про карту: маршрутні точки, розрахований і
    фактичний трек дрона. Призначений для повторного використання в інших
    тестах — не потребує знання маршруту наперед."""

    POLL_MS = 300

    def __init__(self) -> None:
        if TkinterMapView is None:
            raise RuntimeError("tkintermapview не встановлено: pip install tkintermapview")

        QueueItem = tuple[Optional[Position], Optional[Position], list[Position], Optional[Position]]
        self._queue: "queue.Queue[QueueItem]" = queue.Queue(maxsize=1)
        self._running = True
        self._ready = threading.Event()

        self._targets: list[Position] = []
        self._markers_initialized = False
        self._wp_markers: list = []
        self._wp_status: list[str] = []

        self._path_points: list[tuple[float, float]] = []
        self._path_line = None
        self._drone_marker = None
        self._real_path_points: list[tuple[float, float]] = []
        self._real_path_line = None

        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=10.0)

    # ── Публічний API — викликається з головного циклу (іншого потоку) ─────

    def update(
        self,
        calculated_position: Optional[Position],
        actual_position: Optional[Position],
        targets: list[Position],
        current_target: Optional[Position],
    ) -> None:
        """Неблокуюче: кладе найсвіжіший стан у чергу (розмір 1 — старий стан,
        який вікно не встигло забрати, просто заміняється новим)."""
        item = (calculated_position, actual_position, targets, current_target)
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
        self.root.title("UAV route map")
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

    def _init_markers(self, targets: list[Position]) -> None:
        self._targets = targets
        self._wp_status = ["pending"] * len(targets)
        self._wp_markers = [
            self._make_waypoint_marker(i, lat, lon, "pending")
            for i, (lat, lon, _alt) in enumerate(targets)
        ]

        if len(targets) >= 2:
            # fit_bounding_box вимагає СПРАВЖНІй прямокутник (top-left != bottom-right).
            lats = [t[0] for t in targets]
            lons = [t[1] for t in targets]
            self.map_widget.fit_bounding_box((max(lats), min(lons)), (min(lats), max(lons)))
        elif targets:
            self.map_widget.set_position(targets[0][0], targets[0][1])

        self._markers_initialized = True

    def _make_waypoint_marker(self, index: int, lat: float, lon: float, status: str):
        circle, outside = _WAYPOINT_COLORS[status]
        return self.map_widget.set_marker(
            lat, lon, text=f"WP{index + 1}",
            marker_color_circle=circle, marker_color_outside=outside,
        )

    def _poll(self) -> None:
        try:
            while True:
                calculated_position, actual_position, targets, current_target = self._queue.get_nowait()
                self._apply(calculated_position, actual_position, targets, current_target)
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
        calculated_position: Optional[Position],
        actual_position: Optional[Position],
        targets: list[Position],
        current_target: Optional[Position],
    ) -> None:
        if not self._markers_initialized and targets:
            self._init_markers(targets)

        if self._targets:
            current_index = len(self._targets) if current_target is None else self._targets.index(current_target)
            # tkintermapview не має "змінити колір маркера" — найпростіше перестворити
            # маркер, якщо його статус справді змінився (рідкісна подія, не щотік).
            for i, (lat, lon, _alt) in enumerate(self._targets):
                status = "reached" if i < current_index else ("current" if i == current_index else "pending")
                if status != self._wp_status[i]:
                    self._wp_status[i] = status
                    self._wp_markers[i].delete()
                    self._wp_markers[i] = self._make_waypoint_marker(i, lat, lon, status)

        if calculated_position is not None:
            lat, lon, _alt = calculated_position
            self._append_if_moved(self._path_points, lat, lon)

            if len(self._path_points) >= 2:
                if self._path_line is None:
                    self._path_line = self.map_widget.set_path(self._path_points, color=_DRONE_COLOR[0], width=3)
                else:
                    self._path_line.set_position_list(self._path_points)

            if self._drone_marker is None:
                self._drone_marker = self.map_widget.set_marker(
                    lat, lon, text="",
                    marker_color_circle=_DRONE_COLOR[0], marker_color_outside=_DRONE_COLOR[1],
                )
            else:
                self._drone_marker.set_position(lat, lon)

        if actual_position is not None:
            lat, lon, _alt = actual_position
            self._append_if_moved(self._real_path_points, lat, lon)

            if len(self._real_path_points) >= 2:
                if self._real_path_line is None:
                    self._real_path_line = self.map_widget.set_path(
                        self._real_path_points, color=_REAL_PATH_COLOR, width=3
                    )
                else:
                    self._real_path_line.set_position_list(self._real_path_points)
