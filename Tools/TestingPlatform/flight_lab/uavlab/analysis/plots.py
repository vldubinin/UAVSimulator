"""PNG-графіки для звіту та для мультимодального аналізу Claude."""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

from .metrics import DEG_M, _col, resample_uniform  # noqa: E402

DPI = 100


def _time(df: pd.DataFrame, t0: float) -> np.ndarray:
    return df["sim_time"].to_numpy(dtype=float) - t0


def _save(fig, path: Path) -> Path:
    fig.tight_layout()
    fig.savefig(path, dpi=DPI)
    plt.close(fig)
    return path


def _shade_anomalies(ax, anomalies: list[dict]) -> None:
    for a in anomalies:
        if a.get("kind") == "inside_ew_zone":
            ax.axvspan(a["t_start"], a["t_end"], color="#8e24aa", alpha=0.08)
        elif a.get("kind") in ("collision", "controller_exception"):
            ax.axvline(a["t_start"], color="#d32f2f", lw=1.2, ls="--")


def make_plots(session: dict, metrics: dict, out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    truth, steps = session["truth"], session["steps"]
    if truth.empty or "sim_time" not in truth.columns:
        return []
    truth = truth.sort_values("sim_time")
    t0 = float(truth["sim_time"].iloc[0])
    t = _time(truth, t0)
    anomalies = metrics.get("anomalies", [])
    paths: list[Path] = []

    # 1. Траєкторія (план) + профіль висоти
    lat, lon, alt = _col(truth, "geo.latitude"), _col(truth, "geo.longitude"), _col(truth, "geo.altitude_m")
    if lat is not None and lon is not None:
        east = (lon - lon[0]) * np.cos(np.radians(lat[0])) * DEG_M
        north = (lat - lat[0]) * DEG_M
        fig, (a1, a2) = plt.subplots(1, 2, figsize=(12, 5), gridspec_kw={"width_ratios": [1, 1.4]})
        sc = a1.scatter(east, north, c=t, s=3, cmap="viridis")
        a1.plot(east[0], north[0], "go", ms=8, label="старт")
        a1.plot(east[-1], north[-1], "rs", ms=8, label="фініш")
        a1.set_aspect("equal", adjustable="datalim")
        a1.set_xlabel("схід, м"); a1.set_ylabel("північ, м"); a1.set_title("Траєкторія (план)")
        a1.legend(loc="best", fontsize=8); fig.colorbar(sc, ax=a1, label="t, с")
        if alt is not None:
            a2.plot(t, alt, lw=1.2)
            a2.set_xlabel("t, с"); a2.set_ylabel("висота, м"); a2.set_title("Висота")
            _shade_anomalies(a2, anomalies)
        paths.append(_save(fig, out_dir / "01_trajectory.png"))

    # 2. Відпрацювання уставок (внутрішній контур)
    pairs = [("attitude_deg.roll", "autopilot.target.roll_deg", "Крен, °"),
             ("attitude_deg.pitch", "autopilot.target.pitch_deg", "Тангаж, °"),
             ("yaw_rate_world_dps", "autopilot.target.yaw_rate_dps", "Швидкість рискання, °/с")]
    fig, axes = plt.subplots(3, 1, figsize=(12, 8), sharex=True)
    for ax, (act, tgt, label) in zip(axes, pairs):
        x, y = _col(truth, act), _col(truth, tgt)
        if y is not None:
            ax.plot(t, y, color="#e53935", lw=1.0, label="уставка (алгоритм)")
        if x is not None:
            ax.plot(t, x, color="#1e88e5", lw=1.0, label="факт")
        ax.set_ylabel(label); ax.grid(alpha=0.3); _shade_anomalies(ax, anomalies)
        ax.legend(loc="upper right", fontsize=8)
    axes[-1].set_xlabel("t, с"); axes[0].set_title("Уставка алгоритму vs фактична орієнтація")
    paths.append(_save(fig, out_dir / "02_attitude_tracking.png"))

    # 3. Команди алгоритму + вихід внутрішнього PID
    if not steps.empty and "sim_time" in steps.columns:
        st = _time(steps, t0)
        fig, axes = plt.subplots(4, 1, figsize=(12, 9), sharex=True)
        for ax, (c, label) in zip(axes, [("cmd.roll_deg", "крен, °"), ("cmd.pitch_deg", "тангаж, °"),
                                         ("cmd.yaw_rate_deg_s", "рискання, °/с"), ("cmd.thrust", "тяга [0,1]")]):
            x = _col(steps, c)
            if x is not None:
                ax.step(st, x, where="post", lw=1.0)
            ax.set_ylabel(label); ax.grid(alpha=0.3); _shade_anomalies(ax, anomalies)
        thr = _col(truth, "throttle")
        if thr is not None:
            axes[3].plot(t, thr, color="#fb8c00", lw=1.0, label="фактичний газ")
            axes[3].legend(loc="upper right", fontsize=8)
        axes[0].set_title("Команди алгоритму (SET_ATTITUDE_TARGET)"); axes[-1].set_xlabel("t, с")
        paths.append(_save(fig, out_dir / "03_commands.png"))

    # 4. Оболонка польоту
    fig, axes = plt.subplots(3, 1, figsize=(12, 7), sharex=True)
    for ax, (c, label) in zip(axes, [("airspeed_mps", "повітряна швидкість, м/с"), ("aoa_deg", "кут атаки, °"),
                                     ("load_factor_g", "перевантаження, g")]):
        x = _col(truth, c)
        if x is not None:
            ax.plot(t, x, lw=1.0)
        if c == "aoa_deg":
            ax.axhline(15, color="r", ls=":", lw=1); ax.axhline(-15, color="r", ls=":", lw=1)
        ax.set_ylabel(label); ax.grid(alpha=0.3); _shade_anomalies(ax, anomalies)
    axes[0].set_title("Оболонка польоту"); axes[-1].set_xlabel("t, с")
    paths.append(_save(fig, out_dir / "04_envelope.png"))

    # 5. Спектр коливань крену/тангажу
    fig, ax = plt.subplots(figsize=(12, 4))
    plotted = False
    for c, label in (("attitude_deg.roll", "крен"), ("attitude_deg.pitch", "тангаж")):
        x = _col(truth, c)
        if x is None:
            continue
        tu, xu = resample_uniform(t, x, 20.0)
        if xu.size < 160:
            continue
        d = xu - np.convolve(xu, np.ones(80) / 80, mode="same")
        d = d[80:-80]
        if d.size < 80:
            continue
        spec = np.abs(np.fft.rfft(d * np.hanning(d.size))) ** 2
        f = np.fft.rfftfreq(d.size, 1 / 20.0)
        band = (f >= 0.05) & (f <= 5)
        ax.semilogy(f[band], spec[band] + 1e-9, label=label)
        plotted = True
    if plotted:
        ax.set_xlabel("частота, Гц"); ax.set_ylabel("потужність"); ax.grid(alpha=0.3, which="both")
        ax.set_title("Спектр коливань орієнтації (без повільного тренду)"); ax.legend()
        paths.append(_save(fig, out_dir / "05_spectrum.png"))
    else:
        plt.close(fig)

    return paths
