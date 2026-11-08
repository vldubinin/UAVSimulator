"""
Контракт алгоритму керування.

Користувач пише ОДИН файл з підкласом Controller (або просто функцією `step(obs)`):

    class MyAutopilot(Controller):
        rate_hz = 20                       # опційно; дефолт — з конфігу (control_rate_hz)

        def on_start(self, ctx):           # опційно
            self.kp = ctx.params.get("kp", 0.3)

        def step(self, obs) -> Command:    # обов'язково
            ctx = self.ctx
            err = 120 - obs.truth.alt_m
            ctx.log("alt_error_m", err)    # внутрішні змінні -> телеметрія + LLM-аналіз
            return Command(pitch_deg=self.kp * err, thrust=0.7)

        def is_done(self, obs) -> bool:    # опційно: мету досягнуто -> тест зупиняється
            return False

Command: кути в градусах (крен/тангаж — абсолютні уставки, yaw_rate — °/с), thrust — [0,1].
Ці уставки відпрацьовує внутрішній PID UAttitudeControlComponent в Unreal.
"""

from __future__ import annotations

import importlib.util
import inspect
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional


@dataclass
class Command:
    roll_deg: float = 0.0
    pitch_deg: float = 0.0
    yaw_rate_deg_s: float = 0.0
    thrust: float = 0.5


@dataclass
class Context:
    """Те, що стенд дає алгоритму: маніфест умов тесту, параметри з конфігу, логування."""
    manifest: dict = field(default_factory=dict)
    params: dict = field(default_factory=dict)
    session_id: str = ""
    session_dir: Optional[Path] = None
    _logs: dict[str, Any] = field(default_factory=dict, repr=False)
    _events: list[dict] = field(default_factory=list, repr=False)
    _map_ops: dict[str, Any] = field(default_factory=dict, repr=False)

    def log(self, name: str, value: Any) -> None:
        """Записати внутрішню змінну алгоритму на поточному кроці (число, рядок, bool)."""
        self._logs[str(name)] = value

    def event(self, kind: str, **data) -> None:
        """Подія алгоритму (зміна режиму, досягнення точки маршруту...) — потрапляє в events.jsonl і в аналіз."""
        self._events.append({"kind": kind, **data})

    def map_marker(self, name: str, lat: float, lon: float, label: Optional[str] = None,
                   color: str = "orange") -> None:
        """Іменований маркер на живій карті (--map): створюється або переміщується за ім'ям.
        color: orange | purple | red | green | blue | gray. Напр. власна оцінка позиції алгоритмом."""
        self._map_ops[str(name)] = {"lat": float(lat), "lon": float(lon), "label": label, "color": color}

    def map_remove(self, name: str) -> None:
        self._map_ops[str(name)] = None

    def _take_map(self) -> dict[str, Any]:
        ops, self._map_ops = self._map_ops, {}
        return ops

    def _take(self) -> tuple[dict, list[dict]]:
        logs, events = self._logs, self._events
        self._logs, self._events = {}, []
        return logs, events


class Controller:
    rate_hz: Optional[float] = None
    ctx: Context

    def on_start(self, ctx: Context) -> None:
        pass

    def step(self, obs) -> Optional[Command]:
        raise NotImplementedError

    def is_done(self, obs) -> bool:
        return False

    def on_stop(self, ctx: Context) -> None:
        pass


class _FunctionController(Controller):
    """Обгортка для файлу з функцією step(obs) [+ опційні on_start(ctx)/on_stop(ctx)/is_done(obs)]."""

    def __init__(self, module) -> None:
        self._m = module
        self.rate_hz = getattr(module, "RATE_HZ", None)

    def on_start(self, ctx):
        fn: Optional[Callable] = getattr(self._m, "on_start", None)
        if fn:
            fn(ctx)

    def step(self, obs):
        params = inspect.signature(self._m.step).parameters
        return self._m.step(obs, self.ctx) if len(params) >= 2 else self._m.step(obs)

    def is_done(self, obs):
        fn = getattr(self._m, "is_done", None)
        return bool(fn(obs)) if fn else False

    def on_stop(self, ctx):
        fn = getattr(self._m, "on_stop", None)
        if fn:
            fn(ctx)


def load_controller(path: str | Path, class_name: Optional[str] = None) -> Controller:
    path = Path(path).resolve()
    if not path.is_file():
        raise FileNotFoundError(f"Файл алгоритму не знайдено: {path}")

    # Каталог алгоритму — у sys.path, щоб працювали його локальні імпорти (common.py тощо).
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(f"uavlab_user_{path.stem}", path)
    module = importlib.util.module_from_spec(spec)
    # Реєструємо в sys.modules: так модуль бачать pickle/dataclasses і uavlab.simclock (підміна `time`).
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    candidates = [
        obj for _, obj in inspect.getmembers(module, inspect.isclass)
        if issubclass(obj, Controller) and obj is not Controller and obj.__module__ == module.__name__
    ]
    if class_name:
        candidates = [c for c in candidates if c.__name__ == class_name]
    if len(candidates) == 1:
        return candidates[0]()
    if len(candidates) > 1:
        names = ", ".join(c.__name__ for c in candidates)
        raise ValueError(f"У {path.name} кілька підкласів Controller ({names}) — вкажіть controller_class у конфігу")
    if callable(getattr(module, "step", None)):
        return _FunctionController(module)
    raise ValueError(f"У {path.name} немає ні підкласу uavlab.Controller, ні функції step(obs)")
