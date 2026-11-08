"""
Збирає пакет для LLM з записаної сесії: конфіг/мета тесту, маніфест умов, метрики,
події, аномалії, даунсемпльовані ряди + вікна навколо аномалій у вищій роздільності,
код алгоритму. Бюджет розміру регулюється обрізанням РЯДІВ, а не метрик.
"""

from __future__ import annotations

import json
import re
from typing import Any

import numpy as np
import pandas as pd

MAX_OVERVIEW_ROWS = 240
WINDOW_HALF_S = 4.0
WINDOW_MAX_S = 20.0
WINDOW_HZ = 5.0
MAX_WINDOWS = 4
MAX_LOG_ROWS = 150
MAX_LOG_TRANSITIONS = 60
MAX_SOURCE_CHARS = 60_000
MAX_EVENTS = 80
# Загальний бюджет тексту пакета (~3 символи/токен для CSV з числами => ~30-35k токенів).
MAX_TOTAL_CHARS = 100_000

# Пріоритет аномалій для вікон високої роздільності (менше = важливіше).
ANOMALY_PRIORITY = {"collision": 0, "controller_exception": 0, "high_aoa_stall_risk": 1, "command_link_stale": 2,
                    "telemetry_gap": 2, "aileron_pid_saturated": 3, "elevator_pid_saturated": 3,
                    "roll_tracking_error": 4, "pitch_tracking_error": 4}

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
    constant = {}
    for short, full in TRUTH_COLUMNS.items():
        if full in df.columns:
            v = pd.to_numeric(df[full], errors="coerce").to_numpy(dtype=float)[idx]
            if not np.isfinite(v).any():
                continue
            if np.nanmax(v) - np.nanmin(v) < 1e-3:  # стала колонка — одним значенням у заголовку, не рядками
                constant[short] = round(float(np.nanmean(v)), 3)
            else:
                cols[short] = np.round(v, 2)
    head = f"# сталі в цьому інтервалі: {json.dumps(constant)}\n" if constant else ""
    return head + pd.DataFrame(cols).to_csv(index=False)


def _merge_windows(anomalies: list[dict], dur: float) -> list[tuple[float, float, list[dict]]]:
    """Вікна навколо найважливіших аномалій; аномалії, що перекриваються, — в одне вікно."""
    ranked = sorted((a for a in anomalies if a.get("kind") != "inside_ew_zone"),
                    key=lambda a: (ANOMALY_PRIORITY.get(a.get("kind"), 5), a.get("t_start") or 0.0))
    windows: list[tuple[float, float, list[dict]]] = []
    for a in ranked:
        s = max(0.0, (a.get("t_start") or 0.0) - WINDOW_HALF_S)
        e = min(dur, max((a.get("t_end") or 0.0) + WINDOW_HALF_S, s + 2 * WINDOW_HALF_S))
        e = min(e, s + WINDOW_MAX_S)
        for i, (ws, we, items) in enumerate(windows):
            if s <= we and e >= ws:  # перетин — розширюємо наявне вікно (у межах ліміту)
                ns = min(ws, s)
                windows[i] = (ns, min(max(we, e), ns + WINDOW_MAX_S), items + [a])
                break
        else:
            if len(windows) < MAX_WINDOWS:
                windows.append((s, e, [a]))
    return sorted(windows, key=lambda w: w[0])


def _log_overview(steps: pd.DataFrame, t0: float) -> str:
    """ctx.log алгоритму: числові змінні — даунсемпл, текстові — лише моменти зміни категорії
    (числа всередині рядка не вважаються зміною: «38.1° off course» і «40.2° off course» — одна категорія)."""
    log_cols = [c for c in steps.columns if c.startswith("log.")]
    t = (steps["sim_time"] - t0).round(2)
    num, txt = [], []
    for c in log_cols:
        (num if pd.api.types.is_numeric_dtype(steps[c]) else txt).append(c)
    out = []
    if num:
        n = max(1, len(steps) // MAX_LOG_ROWS)
        sub = steps.iloc[::n][num].copy().round(3)
        sub.insert(0, "t", t.iloc[::n])
        out.append("числові змінні:\n" + sub.rename(columns=lambda c: c.removeprefix("log.")).to_csv(index=False))
    for c in txt:
        vals = steps[c].astype(str)
        keys = vals.map(lambda v: re.sub(r"[-+]?\d+(?:\.\d+)?", "#", v))
        idx = list(np.where(keys.ne(keys.shift()).to_numpy())[0])
        rows = [f"{t.iloc[i]}: {vals.iloc[i]}" for i in idx[:MAX_LOG_TRANSITIONS]]
        more = f"\n... ще {len(idx) - MAX_LOG_TRANSITIONS} змін" if len(idx) > MAX_LOG_TRANSITIONS else ""
        out.append(f"{c.removeprefix('log.')} - зміни категорії ({len(idx)}):\n" + "\n".join(rows) + more)
    return "\n\n".join(out)


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

        parts["anomaly_windows"] = [
            {"anomalies": items, "interval": [round(s, 2), round(e, 2)], "hz": f"<= {WINDOW_HZ}",
             "csv": _table(truth, t0, s, e, WINDOW_HZ)}
            for s, e, items in _merge_windows(metrics.get("anomalies", []), dur)]

    steps: pd.DataFrame = session["steps"]
    if not steps.empty:
        if any(c.startswith("log.") for c in steps.columns) and "sim_time" in steps.columns and not truth.empty:
            parts["controller_log_overview"] = _log_overview(steps, float(truth["sim_time"].iloc[0]))

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
                       f"інтервал t={w['interval']}, hz={w['hz']}; аномалії: "
                       f"{json.dumps(w['anomalies'], ensure_ascii=False)}\n{w['csv']}"))
    if "controller_log_overview" in pack:
        blocks.append(("controller_log_overview", pack["controller_log_overview"]))
    if "controller_source" in pack:
        cs = pack["controller_source"]
        text = "\n\n".join(f"# ===== {name}{' (ENTRY)' if name == cs['entry'] else ''} =====\n{code}"
                           for name, code in cs["files"].items())
        blocks.append(("controller_source", text))

    # Бюджет: спершу прибираємо найменш важливі вікна аномалій, потім проріджуємо огляд —
    # метрики, маніфест і код не чіпаємо.
    def total() -> int:
        return sum(len(t) for _, t in blocks)

    while total() > MAX_TOTAL_CHARS:
        wins = [i for i, (n, _) in enumerate(blocks) if n.startswith("anomaly_window_")]
        if not wins:
            break
        blocks.pop(wins[-1])
    for i, (n, t) in enumerate(blocks):
        if n == "timeseries_overview" and total() > MAX_TOTAL_CHARS:
            lines = t.split("\n")
            head, rows = lines[:3], lines[3:]
            keep = max(60, len(rows) * MAX_TOTAL_CHARS // max(total(), 1))
            step = max(1, len(rows) // keep)
            blocks[i] = (n, "\n".join(head + rows[::step]) + f"\n# проріджено x{step} для бюджету")
    return blocks
