"""
Збирає пакет для LLM з записаної сесії: конфіг/мета тесту, маніфест умов, метрики,
події, аномалії, даунсемпльовані ряди + вікна навколо аномалій у вищій роздільності,
код алгоритму. Бюджет розміру регулюється обрізанням РЯДІВ, а не метрик.
"""

from __future__ import annotations

import json
from typing import Any

import numpy as np
import pandas as pd

MAX_OVERVIEW_ROWS = 400
WINDOW_HALF_S = 5.0
WINDOW_HZ = 10.0
MAX_WINDOWS = 8
MAX_SOURCE_CHARS = 60_000
MAX_EVENTS = 80

TRUTH_COLUMNS = {
    "alt_m": "geo.altitude_m",
    "airspeed": "airspeed_mps",
    "aoa": "aoa_deg",
    "roll": "attitude_deg.roll",
    "roll_tgt": "autopilot.target.roll_deg",
    "pitch": "attitude_deg.pitch",
    "pitch_tgt": "autopilot.target.pitch_deg",
    "yaw_rate": "yaw_rate_world_dps",
    "yaw_rate_tgt": "autopilot.target.yaw_rate_dps",
    "heading": "heading_deg",
    "thr_tgt": "autopilot.target.thrust",
    "thr": "throttle",
    "ail": "autopilot.pid_output.aileron",
    "elev": "autopilot.pid_output.elevator",
    "rud": "autopilot.pid_output.rudder",
    "nz_g": "load_factor_g",
    "cmd_age": "autopilot.last_command_age_s",
    "wind": "environment.wind_speed_mps",
    "ew": "environment.ew_intensity",
}


def _table(df: pd.DataFrame, t0: float, t_from: float, t_to: float, hz: float) -> str:
    """Рівномірно передискретизована CSV-таблиця ключових сигналів truth у [t_from, t_to]."""
    t = df["sim_time"].to_numpy(dtype=float) - t0
    if len(t) > 3:
        hz = min(hz, 1.0 / max(float(np.median(np.diff(t))), 1e-3))  # не частіше за фактичну частоту шини
    grid = np.arange(t_from, t_to + 1e-9, 1.0 / hz)
    if grid.size == 0:
        return ""
    idx = np.clip(np.searchsorted(t, grid), 0, len(t) - 1)  # найближчий наступний семпл (без інтерполяції кутів)
    cols = {"t": np.round(grid, 2)}
    for short, full in TRUTH_COLUMNS.items():
        if full in df.columns:
            v = pd.to_numeric(df[full], errors="coerce").to_numpy(dtype=float)[idx]
            if np.isfinite(v).any():
                cols[short] = np.round(v, 2)
    return pd.DataFrame(cols).to_csv(index=False)


def _strip_manifest(manifest: dict) -> dict:
    """Маніфест без шуму: прибираємо службові поля PID-налагодження (Integral, PreviousError...)."""
    m = json.loads(json.dumps(manifest))
    for a in m.get("aircraft", []):
        ap = a.get("autopilot", {})
        for key in ("roll_pid", "pitch_pid", "yaw_rate_pid"):
            pid = ap.get(key)
            if isinstance(pid, dict):
                for k in ("integral", "previousError", "previousMeasurement", "filteredDerivative",
                          "lastOutput", "bHasPrevious"):
                    pid.pop(k, None)
    return m


def build_pack(session: dict, metrics: dict, include_source: bool = True) -> dict[str, Any]:
    truth: pd.DataFrame = session["truth"]
    cfg = session["config"] or {}
    parts: dict[str, Any] = {
        "test": {
            "name": cfg.get("name"),
            "objective": cfg.get("objective") or "(не задано — оціни загальну якість і безпеку керування)",
            "start_mode": cfg.get("start"),
            "simulator_mode": cfg.get("mode"),
            "use_truth": cfg.get("use_truth"),
            "control_rate_hz": cfg.get("control_rate_hz"),
            "controller_params": cfg.get("controller_params"),
            "stop_criteria": cfg.get("stop"),
        },
        "run_summary": session["summary"],
        "manifest": _strip_manifest(session["manifest"]),
        "metrics": metrics,
        "events": [e for e in session["events"] if e.get("type") != "session_start"][:MAX_EVENTS],
    }

    if not truth.empty and "sim_time" in truth.columns:
        truth = truth.sort_values("sim_time")
        t0 = float(truth["sim_time"].iloc[0])
        dur = float(truth["sim_time"].iloc[-1]) - t0
        hz = min(2.0, MAX_OVERVIEW_ROWS / max(dur, 1.0))
        parts["timeseries_overview"] = {"hz": round(hz, 3), "t_origin_note": "t — секунди від початку запису",
                                        "columns": TRUTH_COLUMNS, "csv": _table(truth, t0, 0.0, dur, hz)}

        windows = []
        interesting = [a for a in metrics.get("anomalies", []) if a.get("kind") != "inside_ew_zone"]
        for a in interesting[:MAX_WINDOWS]:
            s = max(0.0, a["t_start"] - WINDOW_HALF_S)
            e = min(dur, a["t_end"] + WINDOW_HALF_S)
            if e - s > 40:
                e = s + 40
            windows.append({"anomaly": a, "hz": f"<= {WINDOW_HZ} (не частіше за шину)", "csv": _table(truth, t0, s, e, WINDOW_HZ)})
        parts["anomaly_windows"] = windows

    steps: pd.DataFrame = session["steps"]
    if not steps.empty:
        log_cols = [c for c in steps.columns if c.startswith("log.")]
        if log_cols and "sim_time" in steps.columns and not truth.empty:
            t0 = float(truth["sim_time"].iloc[0])
            n = max(1, len(steps) // MAX_OVERVIEW_ROWS)
            sub = steps.iloc[::n][["sim_time"] + log_cols].copy()
            sub["sim_time"] = (sub["sim_time"] - t0).round(2)
            parts["controller_log_overview"] = sub.rename(columns={"sim_time": "t"}).to_csv(index=False)

    if include_source:
        src = session.get("sources", {})
        entry = session.get("entry")
        ordered = sorted(src.items(), key=lambda kv: (kv[0] != entry, kv[0]))
        budget, files = MAX_SOURCE_CHARS, {}
        for name, text in ordered:
            if budget <= 0:
                break
            files[name] = text[:budget]
            budget -= len(files[name])
        parts["controller_source"] = {"entry": entry, "files": files}

    return parts


def pack_to_text(pack: dict) -> list[tuple[str, str]]:
    """Розбиває пакет на іменовані текстові блоки (в XML-подібних тегах для Claude)."""
    blocks: list[tuple[str, str]] = []
    for key in ("test", "run_summary", "manifest", "metrics", "events"):
        if key in pack:
            blocks.append((key, json.dumps(pack[key], ensure_ascii=False, indent=1, default=str)))
    if "timeseries_overview" in pack:
        ts = pack["timeseries_overview"]
        blocks.append(("timeseries_overview",
                       f"hz={ts['hz']}; {ts['t_origin_note']}; колонки: "
                       f"{json.dumps(ts['columns'], ensure_ascii=False)}\n{ts['csv']}"))
    for i, w in enumerate(pack.get("anomaly_windows", []), start=1):
        blocks.append((f"anomaly_window_{i}",
                       f"{json.dumps(w['anomaly'], ensure_ascii=False)}  hz={w['hz']}\n{w['csv']}"))
    if "controller_log_overview" in pack:
        blocks.append(("controller_log_overview", pack["controller_log_overview"]))
    if "controller_source" in pack:
        cs = pack["controller_source"]
        text = "\n\n".join(f"# ===== {name}{' (ENTRY)' if name == cs['entry'] else ''} =====\n{code}"
                           for name, code in cs["files"].items())
        blocks.append(("controller_source", text))
    return blocks
