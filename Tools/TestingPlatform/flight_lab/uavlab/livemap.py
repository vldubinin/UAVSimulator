"""
Жива карта тесту: OpenStreetMap (tkintermapview) з літаком, його треком, точками маршруту та
маркерами алгоритму. Вмикається `uavlab run ... --map` або `viz.enabled: true` у конфігу.

Карта працює в ОКРЕМОМУ процесі: не гальмує цикл керування, а закриття вікна не зупиняє тест.
Стенд шле лише останній стан (черга на 1 повідомлення, старі відкидаються); іменовані маркери
алгоритму накопичуються на боці стенда й доходять з наступним повідомленням, тож не губляться.

Що на карті:
  * літак — синій маркер з підписом (висота, швидкість, курс) + стрілка курсу, синій трек;
  * точки маршруту з controller_params.<viz.waypoints_param> (дефолт `waypoints`: [[lat, lon, alt], ...])
    з колами радіуса захоплення (<viz.radius_param>, дефолт `waypoint_radius_m`); колір за статусом:
    сірий — попереду, червоний — поточна ціль, зелений — взята. Прогрес — з ctx.log("waypoints_reached")
    або ctx.log("waypoint_index");
  * зони РЕБ з маніфесту (червоні кола);
  * маркери алгоритму: ctx.map_marker("estimate", lat, lon, label="PnP", color="orange").

Потрібен інтернет для тайлів (OSM, без API-ключа). Без пакета tkintermapview стенд працює без карти.
"""

from __future__ import annotations

import math
import multiprocessing as mp
import queue
import time
from typing import Any, Optional

DEG_M = 111_320.0
COLORS = {  # (заливка кола маркера, обвід)
    "pending": ("#9e9e9e", "#616161"),
    "current": ("#e53935", "#b71c1c"),
    "reached": ("#43a047", "#2e7d32"),
    "aircraft": ("#1e88e5", "#0d47a1"),
}
NAMED_COLORS = {"orange": ("#fb8c00", "#e65100"), "purple": ("#8e24aa", "#4a148c"), "red": COLORS["current"],
                "green": COLORS["reached"], "blue": COLORS["aircraft"], "gray": COLORS["pending"]}
TRAIL_MIN_STEP_M = 5.0
TRAIL_MAX_POINTS = 4000


def _offset(lat: float, lon: float, east_m: float, north_m: float) -> tuple[float, float]:
    return lat + north_m / DEG_M, lon + east_m / (DEG_M * math.cos(math.radians(lat)))


def _circle(lat: float, lon: float, radius_m: float, n: int = 36) -> list[tuple[float, float]]:
    return [_offset(lat, lon, radius_m * math.sin(2 * math.pi * k / n), radius_m * math.cos(2 * math.pi * k / n))
            for k in range(n)]


def _dist_m(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot((b[1] - a[1]) * math.cos(math.radians(a[0])) * DEG_M, (b[0] - a[0]) * DEG_M)


# ── процес вікна ─────────────────────────────────────────────────────────────

USER_AGENT = "uavlab-flight-test-bench/0.1 (UAVSimulator flight test lab)"
OSM_TILES = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"


def _patch_tile_requests() -> None:
    """tkintermapview шле тайл-запити з User-Agent «TkinterMapView», який OSM блокує (замість тайлів
    приходить картинка «Access blocked»). Тайл-політика OSM вимагає власний ідентифікуючий User-Agent —
    підставляємо його лише в цьому процесі, саму бібліотеку не змінюємо."""
    import requests
    import tkintermapview.map_widget as map_widget

    class _Requests:
        def __getattr__(self, name):
            return getattr(requests, name)

        @staticmethod
        def get(url, *args, **kwargs):
            headers = dict(kwargs.pop("headers", None) or {})
            headers["User-Agent"] = USER_AGENT
            kwargs.setdefault("timeout", 15)
            return requests.get(url, *args, headers=headers, **kwargs)

    map_widget.requests = _Requests()


def _window_main(q: "mp.Queue", init: dict) -> None:
    import tkinter as tk
    from tkintermapview import TkinterMapView

    _patch_tile_requests()

    root = tk.Tk()
    root.title(f"uavlab — {init.get('title', 'тест')}")
    root.geometry("1000x780")

    top = tk.Frame(root)
    top.pack(fill="x")
    info = tk.Label(top, text="Очікування телеметрії…", anchor="w", font=("Consolas", 10))
    info.pack(side="left", fill="x", expand=True, padx=6, pady=3)
    follow = tk.BooleanVar(value=bool(init.get("follow", True)))
    tk.Checkbutton(top, text="Стежити за літаком", variable=follow).pack(side="right", padx=6)

    m = TkinterMapView(root, corner_radius=0)
    m.pack(fill="both", expand=True)
    m.set_tile_server(init.get("tile_server") or OSM_TILES)

    wps: list[list[float]] = init.get("waypoints") or []
    radius = init.get("radius_m")
    wp_markers = []
    if len(wps) >= 2:
        m.set_path([(w[0], w[1]) for w in wps], color="#757575", width=2)
    for i, w in enumerate(wps):
        if radius:
            m.set_polygon(_circle(w[0], w[1], radius), outline_color="#9e9e9e", fill_color=None, border_width=1)
        c = COLORS["pending"]
        alt = f" · {w[2]:.0f} м" if len(w) > 2 else ""
        wp_markers.append(m.set_marker(w[0], w[1], text=f"WP{i}{alt}", marker_color_circle=c[0],
                                       marker_color_outside=c[1], text_color="#212121"))
    for z in init.get("ew_zones") or []:
        m.set_polygon(_circle(z["latitude"], z["longitude"], z["radius_m"]), outline_color="#c62828",
                      fill_color=None, border_width=2, name=f"РЕБ {z.get('name', '')}")

    pts = [(w[0], w[1]) for w in wps]
    if len(pts) >= 2:
        lats, lons = [p[0] for p in pts], [p[1] for p in pts]
        pad = 0.003
        m.fit_bounding_box((max(lats) + pad, min(lons) - pad), (min(lats) - pad, max(lons) + pad))
    elif pts:
        m.set_position(*pts[0]); m.set_zoom(15)
    else:
        m.set_zoom(14)

    st: dict[str, Any] = {"aircraft": None, "arrow": None, "trail": None, "trail_pts": [], "named": {},
                          "centered": False, "last_reached": None}

    def apply(msg: dict) -> None:
        lat, lon = msg.get("lat"), msg.get("lon")
        if lat is None or lon is None:
            return
        hdg = msg.get("heading")
        label = (f"{msg.get('alt', 0):.0f} м · {msg.get('airspeed', 0):.0f} м/с"
                 + (f" · {hdg:.0f}°" if hdg is not None else ""))
        if st["aircraft"] is None:
            c = COLORS["aircraft"]
            st["aircraft"] = m.set_marker(lat, lon, text=label, marker_color_circle=c[0], marker_color_outside=c[1],
                                          text_color=c[1])
        else:
            st["aircraft"].set_position(lat, lon)
            st["aircraft"].set_text(label)

        if hdg is not None:  # стрілка курсу, 250 м
            tip = _offset(lat, lon, 250 * math.sin(math.radians(hdg)), 250 * math.cos(math.radians(hdg)))
            if st["arrow"] is None:
                st["arrow"] = m.set_path([(lat, lon), tip], color="#0d47a1", width=3)
            else:
                st["arrow"].set_position_list([(lat, lon), tip])

        trail = st["trail_pts"]
        if not trail or _dist_m(trail[-1], (lat, lon)) >= TRAIL_MIN_STEP_M:
            trail.append((lat, lon))
            if len(trail) > TRAIL_MAX_POINTS:
                del trail[: len(trail) - TRAIL_MAX_POINTS]
            if len(trail) >= 2:
                if st["trail"] is None:
                    st["trail"] = m.set_path(list(trail), color="#1e88e5", width=2)
                else:
                    st["trail"].set_position_list(list(trail))

        reached = msg.get("reached")
        if reached is not None and reached != st["last_reached"]:
            st["last_reached"] = reached
            for i, mk in enumerate(wp_markers):
                key = "reached" if i < reached else ("current" if i == reached else "pending")
                circle, outside = COLORS[key]
                mk.marker_color_circle, mk.marker_color_outside = circle, outside  # для перемальовування
                # draw() лише переміщує вже створені фігури — колір міняємо напряму на canvas.
                if mk.polygon is not None:
                    m.canvas.itemconfig(mk.polygon, fill=outside, outline=outside)
                if mk.big_circle is not None:
                    m.canvas.itemconfig(mk.big_circle, fill=circle, outline=outside)

        for name, op in (msg.get("markers") or {}).items():
            old = st["named"].pop(name, None)
            if old is not None:
                old.delete()
            if op is None:
                continue
            c = NAMED_COLORS.get(op.get("color") or "orange", NAMED_COLORS["orange"])
            st["named"][name] = m.set_marker(op["lat"], op["lon"], text=op.get("label") or name,
                                             marker_color_circle=c[0], marker_color_outside=c[1], text_color=c[1])

        if follow.get() and (st["centered"] or not pts):
            m.set_position(lat, lon)
        st["centered"] = True

        n = len(wps)
        prog = f" | точки {reached}/{n}" if reached is not None and n else ""
        status = f" | {msg['status']}" if msg.get("status") else ""
        info.config(text=f"t={msg.get('t', 0):7.1f} с (сим.){prog} | висота {msg.get('alt', 0):6.1f} м | "
                         f"V {msg.get('airspeed', 0):5.1f} м/с | крен {msg.get('roll', 0):6.1f}°{status}")

    def poll() -> None:
        latest = None
        try:
            while True:
                latest = q.get_nowait()
        except queue.Empty:
            pass
        except (EOFError, OSError):
            root.destroy()
            return
        if latest is not None:
            if latest.get("final"):
                root.title(f"uavlab — {init.get('title', 'тест')} — завершено: {latest.get('reason', '')}")
            apply(latest)
        root.after(100, poll)

    root.after(100, poll)
    root.mainloop()


# ── клієнт на боці стенда ────────────────────────────────────────────────────

class LiveMap:
    def __init__(self, init: dict, update_hz: float = 5.0) -> None:
        self._q: "mp.Queue" = mp.Queue(maxsize=1)
        self._proc = mp.Process(target=_window_main, args=(self._q, init), daemon=True, name="uavlab-map")
        self._proc.start()
        self._period = 1.0 / max(update_hz, 0.5)
        self._last = 0.0
        self._pending_markers: dict[str, Optional[dict]] = {}
        self._last_msg: Optional[dict] = None

    @property
    def alive(self) -> bool:
        return self._proc.is_alive()

    def add_markers(self, ops: dict[str, Optional[dict]]) -> None:
        self._pending_markers.update(ops)

    def update(self, msg: dict, force: bool = False) -> None:
        now = time.monotonic()
        if not self.alive or (not force and now - self._last < self._period):
            return
        msg = dict(msg, markers=dict(self._pending_markers))
        try:
            try:  # лише найсвіжіший стан: старе непрочитане повідомлення викидаємо
                self._q.get_nowait()
            except queue.Empty:
                pass
            self._q.put_nowait(msg)
            self._pending_markers.clear()
            self._last, self._last_msg = now, msg
        except queue.Full:
            pass

    def finish(self, reason: str) -> None:
        if self._last_msg is not None:
            self.update(dict(self._last_msg, final=True, reason=reason), force=True)

    def wait_closed(self) -> None:
        if self.alive:
            print("[uavlab] Карта лишається відкритою — закрийте її вікно, щоб завершити.", flush=True)
            self._proc.join()


def start_live_map(cfg: dict, manifest: dict) -> Optional[LiveMap]:
    viz = cfg.get("viz") or {}
    try:
        import tkintermapview  # noqa: F401  перевірка наявності до запуску процесу
    except ImportError:
        print("[uavlab] Карта недоступна: pip install tkintermapview", flush=True)
        return None
    params = cfg.get("controller_params") or {}
    wps = params.get(viz.get("waypoints_param", "waypoints")) or []
    wps = [list(w) for w in wps if isinstance(w, (list, tuple)) and len(w) >= 2]
    init = {
        "title": cfg.get("name", "тест"),
        "waypoints": wps,
        "radius_m": params.get(viz.get("radius_param", "waypoint_radius_m")),
        "ew_zones": manifest.get("ew_zones") or [],
        "follow": viz.get("follow", True),
        "tile_server": viz.get("tile_server"),
    }
    return LiveMap(init, update_hz=float(viz.get("update_hz", 5.0)))
