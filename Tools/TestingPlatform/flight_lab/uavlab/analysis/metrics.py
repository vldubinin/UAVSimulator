"""
Детерміновані метрики польоту. LLM погано рахує по сирих рядах, тому все числове
рахується тут, а Claude отримує вже готові метрики + аномальні вікна + графіки.

Вхід — DataFrame'и з recorder.load_session(): truth (flight_truth на кожному пакеті
шини) і steps (кроки алгоритму). Усі функції толерантні до відсутніх колонок
(напр. сесія без flight_truth або без автопілота).
"""

from __future__ import annotations

import math
from typing import Any, Optional

import numpy as np
import pandas as pd

DEG_M = 111_320.0

# Пороги для детекції аномалій
ROLL_ERR_ANOMALY_DEG = 15.0
PITCH_ERR_ANOMALY_DEG = 10.0
AOA_HIGH_DEG = 15.0
SAT_LEVEL = 0.98
CMD_AGE_STALE_S = 0.5
GAP_ANOMALY_S = 0.5
MIN_ANOMALY_DURATION_S = 0.3


# ── допоміжне ────────────────────────────────────────────────────────────────

def _col(df: pd.DataFrame, name: str) -> Optional[np.ndarray]:
    if name not in df.columns:
        return None
    s = pd.to_numeric(df[name], errors="coerce").to_numpy(dtype=float)
    return s if np.isfinite(s).any() else None


def _r(x: Any, nd: int = 3) -> Any:
    if x is None:
        return None
    if isinstance(x, (float, np.floating)):
        return None if not math.isfinite(float(x)) else round(float(x), nd)
    if isinstance(x, (np.integer,)):
        return int(x)
    return x


def _stats(x: Optional[np.ndarray]) -> Optional[dict]:
    if x is None:
        return None
    x = x[np.isfinite(x)]
    if x.size == 0:
        return None
    return {"mean": _r(x.mean()), "min": _r(x.min()), "max": _r(x.max()),
            "std": _r(x.std()), "p95_abs": _r(np.percentile(np.abs(x), 95))}


def wrap180(a: np.ndarray) -> np.ndarray:
    return (a + 180.0) % 360.0 - 180.0


def _runs(mask: np.ndarray, t: np.ndarray) -> list[tuple[float, float]]:
    """Неперервні інтервали, де mask == True: [(t_start, t_end), ...]."""
    out: list[tuple[float, float]] = []
    if mask.size == 0:
        return out
    m = np.concatenate([[False], mask.astype(bool), [False]])
    d = np.diff(m.astype(int))
    starts, ends = np.where(d == 1)[0], np.where(d == -1)[0] - 1
    for s, e in zip(starts, ends):
        out.append((float(t[s]), float(t[e])))
    return out


def resample_uniform(t: np.ndarray, x: np.ndarray, hz: float) -> tuple[np.ndarray, np.ndarray]:
    ok = np.isfinite(t) & np.isfinite(x)
    t, x = t[ok], x[ok]
    if t.size < 4:
        return np.array([]), np.array([])
    tu = np.arange(t[0], t[-1], 1.0 / hz)
    return tu, np.interp(tu, t, x)


# ── окремі групи метрик ───────────────────────────────────────────────────────

def tracking_metrics(t: np.ndarray, target: Optional[np.ndarray], actual: Optional[np.ndarray],
                     angular: bool = False) -> Optional[dict]:
    """Ціль (уставка автопілота) vs факт: помилка, зсув, оцінка запізнення (крос-кореляція)."""
    if target is None or actual is None:
        return None
    err = target - actual
    if angular:
        err = wrap180(err)
    ok = np.isfinite(err)
    if ok.sum() < 5:
        return None
    e = err[ok]
    res = {
        "rms_error": _r(np.sqrt(np.mean(e ** 2))),
        "mean_error_bias": _r(e.mean()),
        "max_abs_error": _r(np.abs(e).max()),
        "p95_abs_error": _r(np.percentile(np.abs(e), 95)),
    }
    # Запізнення відгуку: зсув, що максимізує кореляцію target(t) з actual(t + lag)
    tu, xu = resample_uniform(t, target, 20.0)
    _, yu = resample_uniform(t, actual, 20.0)
    if tu.size > 40 and np.std(xu) > 1e-3 and np.std(yu) > 1e-3:
        xu, yu = xu - xu.mean(), yu - yu.mean()
        max_lag = min(60, xu.size // 4)  # до 3 с
        lags = range(0, max_lag)
        corr = [np.dot(xu[: xu.size - k], yu[k:]) / (xu.size - k) for k in lags]
        best = int(np.argmax(corr))
        res["estimated_lag_s"] = _r(best / 20.0)
        res["target_actual_correlation"] = _r(np.corrcoef(xu, yu)[0, 1])
    return res


def step_responses(t: np.ndarray, target: Optional[np.ndarray], actual: Optional[np.ndarray],
                   min_jump: float, settle_band: float, window_s: float = 8.0) -> Optional[dict]:
    """Реакція на ступінчасті зміни уставки: перерегулювання, час наростання 10-90 %, час встановлення."""
    if target is None or actual is None or t.size < 10:
        return None
    jumps = np.where(np.abs(np.diff(target)) >= min_jump)[0] + 1
    items = []
    last_t = -1e9
    for idx in jumps:
        if t[idx] - last_t < 1.0:  # склеюємо пачку дрібних кроків
            continue
        last_t = t[idx]
        t0 = t[idx]
        sp_before, sp = target[idx - 1], target[idx]
        # Вікно — до наступної зміни уставки (або window_s); коротші за 2 с не аналізуємо.
        later = np.where((t > t0) & (np.abs(target - sp) > min_jump * 0.25))[0]
        t_end = min(t0 + window_s, t[later[0]] if later.size else t[-1])
        if t_end - t0 < 2.0:
            continue
        win = (t >= t0) & (t < t_end)
        y = actual[win]
        tw = t[win] - t0
        y0 = actual[idx]
        amp = sp - y0
        if abs(amp) < min_jump * 0.5 or y.size < 5:
            continue
        frac = (y - y0) / amp
        overshoot = max(0.0, float(np.nanmax(frac) - 1.0)) * 100.0
        rise10 = tw[np.argmax(frac >= 0.1)] if (frac >= 0.1).any() else None
        rise90 = tw[np.argmax(frac >= 0.9)] if (frac >= 0.9).any() else None
        outside = np.abs(y - sp) > max(settle_band, 0.1 * abs(amp))
        settle = float(tw[np.where(outside)[0][-1]]) if outside.any() else 0.0
        settled = not outside[-max(1, y.size // 10):].any()
        items.append({
            "t": _r(t0, 2), "from": _r(sp_before, 2), "to": _r(sp, 2), "window_s": _r(t_end - t0, 2),
            "start_value": _r(y0, 2), "reached_fraction": _r(float(np.nanmax(frac)), 2),
            "final_error": _r(float(sp - y[-1]), 2),
            "overshoot_pct": _r(overshoot, 1),
            "rise_time_s": _r(rise90 - rise10, 2) if rise10 is not None and rise90 is not None else None,
            "settling_time_s": _r(settle, 2) if settled else None,
            "settled_within_window": bool(settled),
        })
    if not items:
        return None
    ov = [i["overshoot_pct"] for i in items if i["overshoot_pct"] is not None]
    st = [i["settling_time_s"] for i in items if i["settling_time_s"] is not None]
    return {
        "count": len(items),
        "overshoot_pct_median": _r(float(np.median(ov)), 1) if ov else None,
        "overshoot_pct_max": _r(float(np.max(ov)), 1) if ov else None,
        "settling_time_s_median": _r(float(np.median(st)), 2) if st else None,
        "not_settled_count": sum(1 for i in items if not i["settled_within_window"]),
        "examples": items[:8],
    }


def oscillation_metrics(t: np.ndarray, x: Optional[np.ndarray], hz: float = 20.0) -> Optional[dict]:
    """Домінантна частота коливань (0.1-5 Гц) після видалення повільного тренду."""
    if x is None:
        return None
    tu, xu = resample_uniform(t, x, hz)
    if tu.size < hz * 8:
        return None
    k = int(hz * 4)  # тренд — ковзне середнє 4 с
    trend = np.convolve(xu, np.ones(k) / k, mode="same")
    d = (xu - trend)[k:-k]
    if d.size < hz * 4 or np.std(d) < 1e-6:
        return {"dominant_freq_hz": None, "amplitude": 0.0, "peak_power_share": 0.0}
    spec = np.abs(np.fft.rfft(d * np.hanning(d.size))) ** 2
    freqs = np.fft.rfftfreq(d.size, 1.0 / hz)
    band = (freqs >= 0.1) & (freqs <= 5.0)
    if not band.any():
        return None
    i = np.argmax(np.where(band, spec, 0))
    share = float(spec[max(0, i - 1): i + 2].sum() / max(spec[band].sum(), 1e-12))
    return {
        "dominant_freq_hz": _r(freqs[i], 3),
        "amplitude": _r(np.sqrt(2) * np.std(d)),
        "peak_power_share": _r(share),  # > ~0.3 — виражений гармонічний процес (автоколивання)
    }


def command_smoothness(t: np.ndarray, x: Optional[np.ndarray]) -> Optional[dict]:
    if x is None or t.size < 5:
        return None
    ok = np.isfinite(x) & np.isfinite(t)
    t, x = t[ok], x[ok]
    dt = np.diff(t)
    good = dt > 1e-4
    if good.sum() < 3:
        return None
    rate = np.diff(x)[good] / dt[good]
    dx = np.diff(x)
    dx = dx[np.abs(dx) > 1e-6]
    reversals = int(np.sum(np.diff(np.sign(dx)) != 0)) if dx.size > 1 else 0
    dur = max(t[-1] - t[0], 1e-6)
    return {
        "rate_rms_per_s": _r(np.sqrt(np.mean(rate ** 2))),
        "rate_max_abs_per_s": _r(np.abs(rate).max()),
        "direction_reversals_per_s": _r(reversals / dur, 2),  # високе значення — chattering / шум
        "range": [_r(x.min()), _r(x.max())],
    }


def saturation(t: np.ndarray, x: Optional[np.ndarray], lo: float, hi: float, tol: float = 1e-3) -> Optional[dict]:
    if x is None:
        return None
    ok = np.isfinite(x)
    if not ok.any():
        return None
    mask = ((x <= lo + tol) | (x >= hi - tol)) & ok
    runs = _runs(mask, t)
    longest = max((e - s for s, e in runs), default=0.0)
    return {"fraction": _r(mask.sum() / ok.sum()), "longest_run_s": _r(longest, 2), "runs": len(runs)}


# ── головна функція ───────────────────────────────────────────────────────────

def compute_metrics(session: dict) -> dict:
    truth: pd.DataFrame = session["truth"]
    steps: pd.DataFrame = session["steps"]
    events: list[dict] = session["events"]
    summary: dict = session["summary"]
    m: dict[str, Any] = {"run": {k: summary.get(k) for k in (
        "stop_reason", "sim_duration_s", "wall_duration_s", "truth_samples", "controller_steps",
        "control_rate_hz", "use_truth", "collisions", "controller_exceptions")}}
    anomalies: list[dict] = []

    if truth.empty or "sim_time" not in truth.columns:
        m["warning"] = "flight_truth відсутній — метрики польоту недоступні (увімкніть UFlightTruthComponent)."
        m["anomalies"] = anomalies
        return m

    truth = truth.sort_values("sim_time").reset_index(drop=True)
    t_abs = truth["sim_time"].to_numpy(dtype=float)
    t0 = float(t_abs[0])
    t = t_abs - t0
    m["time_origin_sim_s"] = _r(t0, 3)

    roll, pitch = _col(truth, "attitude_deg.roll"), _col(truth, "attitude_deg.pitch")
    yaw_rate = _col(truth, "yaw_rate_world_dps")
    tgt_roll, tgt_pitch = _col(truth, "autopilot.target.roll_deg"), _col(truth, "autopilot.target.pitch_deg")
    tgt_yaw_rate, tgt_thrust = _col(truth, "autopilot.target.yaw_rate_dps"), _col(truth, "autopilot.target.thrust")
    alt, aoa, airspeed = _col(truth, "geo.altitude_m"), _col(truth, "aoa_deg"), _col(truth, "airspeed_mps")
    lat, lon = _col(truth, "geo.latitude"), _col(truth, "geo.longitude")

    # ── траєкторія / оболонка польоту ───────────────────────────────────
    flight: dict[str, Any] = {
        "altitude_m": _stats(alt), "airspeed_mps": _stats(airspeed), "aoa_deg": _stats(aoa),
        "sideslip_deg": _stats(_col(truth, "sideslip_deg")), "roll_deg": _stats(roll), "pitch_deg": _stats(pitch),
        "load_factor_g": _stats(_col(truth, "load_factor_g")),
        "vertical_speed_mps": _stats(_col(truth, "vertical_speed_mps")),
        "throttle_actual": _stats(_col(truth, "throttle")), "thrust_n": _stats(_col(truth, "thrust_n")),
    }
    if lat is not None and lon is not None:
        east = (lon - lon[0]) * np.cos(np.radians(lat[0])) * DEG_M
        north = (lat - lat[0]) * DEG_M
        seg = np.hypot(np.diff(east), np.diff(north))
        flight["horizontal_distance_travelled_m"] = _r(np.nansum(seg), 1)
        flight["max_distance_from_start_m"] = _r(np.nanmax(np.hypot(east, north)), 1)
        flight["start"] = {"lat": _r(lat[0], 7), "lon": _r(lon[0], 7), "alt_m": _r(alt[0], 1) if alt is not None else None}
        flight["end"] = {"lat": _r(lat[-1], 7), "lon": _r(lon[-1], 7), "alt_m": _r(alt[-1], 1) if alt is not None else None}
    if alt is not None:
        flight["altitude_change_m"] = _r(alt[-1] - alt[0], 1)
    if aoa is not None:
        high = np.abs(aoa) > AOA_HIGH_DEG
        flight["high_aoa_fraction"] = _r(high.mean())
        for s, e in _runs(high, t):
            if e - s >= MIN_ANOMALY_DURATION_S:
                anomalies.append({"kind": "high_aoa_stall_risk", "t_start": _r(s, 2), "t_end": _r(e, 2),
                                  "peak": _r(np.nanmax(np.abs(aoa[(t >= s) & (t <= e)])), 1)})
    m["flight"] = flight

    # ── внутрішній контур (уставка -> факт) ──────────────────────────────
    inner = {
        "roll": tracking_metrics(t, tgt_roll, roll, angular=True),
        "pitch": tracking_metrics(t, tgt_pitch, pitch, angular=True),
        "yaw_rate": tracking_metrics(t, tgt_yaw_rate, yaw_rate),
        "roll_step_response": step_responses(t, tgt_roll, roll, min_jump=5.0, settle_band=2.0),
        "pitch_step_response": step_responses(t, tgt_pitch, pitch, min_jump=3.0, settle_band=1.5),
    }
    m["attitude_tracking"] = inner
    for name, tgt, act, thr in (("roll", tgt_roll, roll, ROLL_ERR_ANOMALY_DEG),
                                ("pitch", tgt_pitch, pitch, PITCH_ERR_ANOMALY_DEG)):
        if tgt is None or act is None:
            continue
        err = np.abs(wrap180(tgt - act))
        for s, e in _runs(err > thr, t):
            if e - s >= MIN_ANOMALY_DURATION_S:
                w = (t >= s) & (t <= e)
                anomalies.append({"kind": f"{name}_tracking_error", "t_start": _r(s, 2), "t_end": _r(e, 2),
                                  "peak_error_deg": _r(np.nanmax(err[w]), 1)})

    # ── коливання ───────────────────────────────────────────────────────
    m["oscillation"] = {
        "roll": oscillation_metrics(t, roll), "pitch": oscillation_metrics(t, pitch),
        "altitude": oscillation_metrics(t, alt), "yaw_rate": oscillation_metrics(t, yaw_rate),
    }

    # ── насичення внутрішнього PID і тяги ───────────────────────────────
    sat = {
        "aileron_pid": saturation(t, _col(truth, "autopilot.pid_output.aileron"), -SAT_LEVEL, SAT_LEVEL, tol=0.0),
        "elevator_pid": saturation(t, _col(truth, "autopilot.pid_output.elevator"), -SAT_LEVEL, SAT_LEVEL, tol=0.0),
        "rudder_pid": saturation(t, _col(truth, "autopilot.pid_output.rudder"), -SAT_LEVEL, SAT_LEVEL, tol=0.0),
        "thrust_target": saturation(t, tgt_thrust, 0.0, 1.0),
    }
    m["saturation"] = sat
    for key, col in (("aileron_pid", "autopilot.pid_output.aileron"), ("elevator_pid", "autopilot.pid_output.elevator")):
        x = _col(truth, col)
        if x is None:
            continue
        for s, e in _runs(np.abs(x) >= SAT_LEVEL, t):
            if e - s >= 2.0:
                anomalies.append({"kind": f"{key}_saturated", "t_start": _r(s, 2), "t_end": _r(e, 2)})

    # ── команди алгоритму користувача ────────────────────────────────────
    if not steps.empty and "sim_time" in steps.columns:
        st = steps["sim_time"].to_numpy(dtype=float) - t0
        cmds = {}
        for c in ("cmd.roll_deg", "cmd.pitch_deg", "cmd.yaw_rate_deg_s", "cmd.thrust"):
            x = _col(steps, c)
            cmds[c.removeprefix("cmd.")] = {"stats": _stats(x), "smoothness": command_smoothness(st, x),
                                            "oscillation": oscillation_metrics(st, x)}
        m["controller_commands"] = cmds

        lat_ms = _col(steps, "step_latency_ms")
        dtw = _col(steps, "dt_wall")
        timing = {"step_latency_ms": _stats(lat_ms)}
        if dtw is not None:
            d = dtw[np.isfinite(dtw)]
            if d.size:
                timing["achieved_rate_hz"] = _r(1.0 / max(d.mean(), 1e-6), 2)
                timing["loop_period_jitter_ms"] = _r(d.std() * 1000.0, 2)
                timing["max_loop_period_ms"] = _r(d.max() * 1000.0, 1)
        if "cmd_sent" in steps.columns:
            timing["commands_dropped_fraction"] = _r(1.0 - steps["cmd_sent"].astype(bool).mean())
        if "cmd_held" in steps.columns:
            timing["steps_without_command_fraction"] = _r(steps["cmd_held"].astype(bool).mean())
        m["controller_timing"] = timing

        # ctx.log(...) змінні алгоритму
        logs = {}
        for c in [c for c in steps.columns if c.startswith("log.")]:
            x = _col(steps, c)
            if x is not None:
                s = _stats(x) or {}
                s["last"] = _r(x[np.isfinite(x)][-1])
                logs[c.removeprefix("log.")] = s
            else:
                vals = steps[c].dropna().astype(str)
                if not vals.empty:
                    vc = vals.value_counts()
                    logs[c.removeprefix("log.")] = {"categorical": True, "last": vals.iloc[-1],
                                                    "top_values": {k: int(v) for k, v in vc.head(8).items()}}
        if logs:
            m["controller_logs"] = logs

    # ── зв'язок / таймінг з боку Unreal ──────────────────────────────────
    link: dict[str, Any] = {}
    age = _col(truth, "autopilot.last_command_age_s")
    if age is not None:
        link["command_age_s"] = _stats(age)
        stale = age > CMD_AGE_STALE_S
        link["stale_command_fraction"] = _r(np.nanmean(stale))
        for s, e in _runs(stale, t):
            if e - s >= MIN_ANOMALY_DURATION_S:
                anomalies.append({"kind": "command_link_stale", "t_start": _r(s, 2), "t_end": _r(e, 2)})
    gaps = np.diff(t)
    if gaps.size:
        link["bus_rate_hz"] = _r(1.0 / max(np.median(gaps), 1e-6), 1)
        link["max_bus_gap_s"] = _r(gaps.max(), 3)
        for i in np.where(gaps > GAP_ANOMALY_S)[0]:
            anomalies.append({"kind": "telemetry_gap", "t_start": _r(t[i], 2), "t_end": _r(t[i + 1], 2)})
    m["link"] = link

    # ── оточення ────────────────────────────────────────────────────────
    ew, wind = _col(truth, "environment.ew_intensity"), _col(truth, "environment.wind_speed_mps")
    env: dict[str, Any] = {"wind_speed_mps": _stats(wind), "ew_intensity": _stats(ew)}
    if ew is not None:
        in_ew = ew > 0.01
        env["time_in_ew_fraction"] = _r(in_ew.mean())
        for s, e in _runs(in_ew, t):
            anomalies.append({"kind": "inside_ew_zone", "t_start": _r(s, 2), "t_end": _r(e, 2)})
        if tgt_roll is not None and roll is not None and in_ew.any() and (~in_ew).any():
            err = np.abs(wrap180(tgt_roll - roll))
            env["roll_rms_error_in_ew"] = _r(np.sqrt(np.nanmean(err[in_ew] ** 2)))
            env["roll_rms_error_outside_ew"] = _r(np.sqrt(np.nanmean(err[~in_ew] ** 2)))
    if wind is not None:
        windy = wind > 0.5
        env["time_in_wind_fraction"] = _r(windy.mean())
        if tgt_roll is not None and roll is not None and windy.any() and (~windy).any():
            err = np.abs(wrap180(tgt_roll - roll))
            env["roll_rms_error_in_wind"] = _r(np.sqrt(np.nanmean(err[windy] ** 2)))
            env["roll_rms_error_outside_wind"] = _r(np.sqrt(np.nanmean(err[~windy] ** 2)))
    m["environment"] = env

    # ── події ───────────────────────────────────────────────────────────
    ev_counts: dict[str, int] = {}
    for ev in events:
        key = ev.get("kind") or ev.get("type") or "unknown"
        ev_counts[key] = ev_counts.get(key, 0) + 1
        if ev.get("kind") in ("collision", "controller_exception") and ev.get("sim_time") is not None:
            ts = float(ev["sim_time"]) - t0
            anomalies.append({"kind": ev["kind"], "t_start": _r(ts, 2), "t_end": _r(ts, 2),
                              "detail": ev.get("other_actor") or (ev.get("traceback") or "")[-300:]})
    m["event_counts"] = ev_counts

    anomalies.sort(key=lambda a: a.get("t_start") or 0.0)
    m["anomalies"] = anomalies[:60]
    m["anomalies_total"] = len(anomalies)
    return m
