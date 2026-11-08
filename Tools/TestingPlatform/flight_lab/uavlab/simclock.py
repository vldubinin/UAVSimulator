"""
Годинник симуляції для коду алгоритму.

Симулятор може йти повільніше (або швидше) за реальний час: при низькому FPS і фіксованому кроці
одна секунда світу Unreal займає кілька секунд на годиннику ПК. Алгоритм, що міряє час через
`time.monotonic()` / `time.time()` / `time.perf_counter()` (фільтри, похідні, таймаути, оцінки курсу
за інтервал), тоді бачить «неправильні» секунди.

`patch_user_modules()` підміняє глобальну назву `time` у модулях алгоритму (файли з теки алгоритму та
`record.extra_sources`) на проксі, у якого monotonic/perf_counter/time/*_ns повертають час СИМУЛЯЦІЇ,
а решта атрибутів (sleep, strftime, ...) — від справжнього модуля. Сам стенд і сторонні бібліотеки не
зачіпаються.

Обмеження: `from time import monotonic` (імпорт функції, а не модуля) не підміняється — у такому коді
використовуйте `obs.t` / `obs.dt` або `uavlab.simclock.now()`.
"""

from __future__ import annotations

import sys
import time as _real_time
from pathlib import Path
from typing import Iterable


class SimClock:
    def __init__(self) -> None:
        self._t: float | None = None
        self._t0: float | None = None
        self._epoch0 = _real_time.time()

    def set(self, sim_time: float) -> None:
        if self._t0 is None:
            self._t0 = sim_time
        self._t = sim_time

    def monotonic(self) -> float:
        """Секунди симуляції від першого кадру тесту (до першого кадру — 0)."""
        return 0.0 if self._t is None else self._t - self._t0

    def time(self) -> float:
        """Епоха (як time.time()), що тече зі швидкістю симуляції від моменту старту стенда."""
        return self._epoch0 + self.monotonic()


CLOCK = SimClock()


def now() -> float:
    return CLOCK.monotonic()


class _SimTimeModule:
    """Проксі модуля time: годинникові функції — час симуляції, решта — справжній модуль."""

    def monotonic(self) -> float:
        return CLOCK.monotonic()

    def perf_counter(self) -> float:
        return CLOCK.monotonic()

    def time(self) -> float:
        return CLOCK.time()

    def monotonic_ns(self) -> int:
        return int(CLOCK.monotonic() * 1e9)

    def perf_counter_ns(self) -> int:
        return int(CLOCK.monotonic() * 1e9)

    def time_ns(self) -> int:
        return int(CLOCK.time() * 1e9)

    def __getattr__(self, name):
        return getattr(_real_time, name)


SIM_TIME_MODULE = _SimTimeModule()


def patch_user_modules(dirs: Iterable[str | Path]) -> list[str]:
    """Підміняє `time` у вже імпортованих модулях з указаних тек. Повертає імена підмінених модулів."""
    roots = [Path(d).resolve() for d in dirs if d]
    patched = []
    for name, mod in list(sys.modules.items()):
        # Сам стенд не чіпаємо (uavlab / uavlab.*), але файл алгоритму вантажиться як «uavlab_user_<ім'я>» — його так.
        if name == "uavlab" or name.startswith("uavlab.") or mod is None:
            continue
        f = getattr(mod, "__file__", None)
        if not f:
            continue
        try:
            p = Path(f).resolve()
        except OSError:
            continue
        if not any(p.is_relative_to(r) for r in roots):
            continue
        if getattr(mod, "time", None) is _real_time:
            mod.time = SIM_TIME_MODULE
            patched.append(name)
    return patched
