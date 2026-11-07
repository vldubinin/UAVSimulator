"""Конфіг тесту (YAML). Усі ключі опційні — нижче дефолти. Умови тесту (місце, оточення,
сенсори) налаштовуються в меню Unreal і приходять у маніфесті — тут їх не дублюємо."""

from __future__ import annotations

import copy
from pathlib import Path
from typing import Any, Optional

import yaml

DEFAULTS: dict[str, Any] = {
    "name": "Untitled flight test",
    "objective": "",                 # критерій успіху словами — ключовий вхід для LLM-аналізу
    "start": "auto",                 # auto — стенд сам шле START; wait — чекати Start у меню Unreal
    "mode": "AutoTrack",             # ESimulatorMode для start: auto (AutoTrack | PlaybackAndAutoTrack)
    "scenario_slot": None,           # слот записаної траєкторії цілі (PlaybackAndAutoTrack)
    "control_rate_hz": 20.0,         # частота виклику step(); Controller.rate_hz має пріоритет
    "use_truth": True,               # False — obs.truth = None (чесний тест лише на сенсорах)
    "controller_class": None,        # якщо у файлі кілька підкласів Controller
    "controller_params": {},         # -> ctx.params
    "stop": {
        "time_limit_s": 180.0,       # симуляційних секунд від першого пакета
        "on_crash": True,            # зупинка на події collision з Unreal
        "max_distance_m": None,      # від точки старту (по горизонталі)
        "min_altitude_m": None,      # абсолютна висота (geo altitude)
        "telemetry_timeout_s": 5.0,  # нема пакетів шини стільки секунд -> зупинка
        "max_step_errors": 1,        # винятків у step() до зупинки
    },
    "endpoints": {
        "sensors": "tcp://127.0.0.1:5555",
        "commands": "tcp://127.0.0.1:5556",
        "control": "tcp://127.0.0.1:5557",
        "events": "tcp://127.0.0.1:5558",
    },
    "record": {
        "sessions_dir": "sessions",  # відносно поточної теки
        "sensors": True,             # сирі JSON-payload'и сенсорів у sensors.jsonl
        "sensors_max_hz": 10.0,      # обмеження частоти запису sensors.jsonl
        "extra_sources": [],         # додаткові .py-файли/теки алгоритму (відносно файлу алгоритму) для LLM
    },
    # PID внутрішнього автопілота на час сесії (Blueprint не змінюється), напр. {"roll": {"kp": 3, "ki": 0.1}}.
    # Поля осі: kp, ki, kd, integral_clamp, derivative_filter_s, invert_output, gain_schedule (keep|disable).
    "autopilot_pid": {},
    # Формальні критерії "пройдено" для uavlab improve: [{metric: "attitude_tracking.roll.rms_error", max: 3}].
    # Порожньо — improve згенерує їх з objective на першій ітерації і заморозить.
    "acceptance": [],
    "improve": {
        "runs_dir": "improve",
        "max_iterations": 5,
        "max_cost_usd": 5.0,               # оцінка за токенами (аналіз + агент)
        "allow": ["params", "code", "autopilot_pid"],
        "approve": False,                  # True — підтверджувати кожну правку в консолі
        "model": "claude-opus-5-5",
        "effort": "high",
        "max_agent_turns": 20,
        "analyze_each_iteration": True,    # повний LLM-аналіз кожної версії (діагноз для агента)
        "validation_configs": [],          # додаткові сценарії для фінальної перевірки найкращої версії
        "pause_between_runs_s": 3.0,
    },
    "analysis": {
        "enabled": True,
        "model": "claude-opus-5-5",
        "effort": "high",
        "language": "uk",
        "max_tokens": 32000,
        "include_plots": True,
        "include_source": True,
    },
}


def _deep_merge(base: dict, override: dict) -> dict:
    out = copy.deepcopy(base)
    for k, v in (override or {}).items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _deep_merge(out[k], v)
        else:
            out[k] = v
    return out


def load_config(path: Optional[str | Path] = None, overrides: Optional[dict] = None) -> dict:
    cfg = copy.deepcopy(DEFAULTS)
    if path:
        with open(path, encoding="utf-8") as f:
            cfg = _deep_merge(cfg, yaml.safe_load(f) or {})
        cfg["_config_path"] = str(Path(path).resolve())
    return _deep_merge(cfg, overrides or {})
