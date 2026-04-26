import sys
import json
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR))
sys.path.insert(0, str(_SCRIPT_DIR.parent / "PyInstall"))

# ---------------------------------------------------------------------------
# Standalone geometry + polar visualiser — no SU2 or Unreal required.
# Adjust these parameters before running.
# ---------------------------------------------------------------------------

PARAMETERS =  ['C:/Users/DubakusPC/Documents/Unreal Projects/UAVSimulator/Content/WingProfile/NACA_2412/naca2412.dat', 'Wing', '4', '0.7', '-45.0', '-45.0', '1', '-6', 'true']
PROFILE_PATH    = PARAMETERS[0]
HINGE_LOCATION  = float(PARAMETERS[3])    # 0 = no flap; chord-fraction, e.g. 0.75
MIN_FLAP        = float(PARAMETERS[4])    # degrees
MAX_FLAP        = float(PARAMETERS[5])    # degrees
FLAP_STEP       = int(PARAMETERS[6])    # degrees, used to pick representative angles to overlay

# ---------------------------------------------------------------------------

import matplotlib
matplotlib.use("TkAgg")   # interactive window; falls back gracefully on headless
import matplotlib.pyplot as plt
import numpy as np

from logger import log
from geometry import load_airfoil_dat, split_and_deflect, generate_mesh


def _plot_outline(ax, coords, color, label=None, linewidth=1.4, alpha=0.9):
    xs = [p[0] for p in coords] + [coords[0][0]]
    ys = [p[1] for p in coords] + [coords[0][1]]
    ax.plot(xs, ys, color=color, linewidth=linewidth, alpha=alpha, label=label)


def _geometry_figure(profile_path: str, hinge_location: float,
                     min_flap: float, max_flap: float, geometry_info) -> plt.Figure:
    base_coords = load_airfoil_dat(profile_path)
    stem = Path(profile_path).stem

    fig, ax = plt.subplots(figsize=(11, 4))
    ax.set_aspect("equal")
    ax.grid(True, linestyle="--", linewidth=0.5, alpha=0.55)
    ax.set_xlabel("x/c")
    ax.set_ylabel("y/c")

    angles = sorted(set([min_flap, 0.0, max_flap]))
    for i, angle in enumerate(angles):
        wing, flap = split_and_deflect(base_coords, hinge_location, angle)
        color = f"C{i}"
        _plot_outline(ax, wing, color=color, label=f"flap {angle:+.1f}°")
        if flap:
            _plot_outline(ax, flap, color=color, alpha=0.75)
        geometry_info.append([wing, flap])  

    ax.axvline(hinge_location, color="black", linestyle="--", linewidth=0.8, alpha=0.5, label=f"hinge x={hinge_location}")
    ax.set_title(
        f"{stem}  |  hinge={hinge_location}  "
        f"flap=[{min_flap:+.1f}°, {max_flap:+.1f}°]"
    )
        

    ax.legend(loc="upper right", fontsize=8)
    fig.tight_layout()
    
    
    return fig


def _find_polar_json(profile_path: str, hinge_location: float,
                     min_flap: float, max_flap: float) -> Path | None:
    stem = Path(profile_path).stem
    name = f"extrapolated_{stem}_{hinge_location}_{min_flap}_{max_flap}.json"
    candidate = _SCRIPT_DIR / name
    return candidate if candidate.exists() else None


def _polar_figure(json_path: Path) -> plt.Figure:
    with json_path.open(encoding="utf-8") as f:
        entries = json.load(f)

    fig, axes = plt.subplots(1, 3, figsize=(15, 4))
    fig.suptitle(f"Extrapolated polar — {json_path.stem}", fontsize=10)

    ax_cl, ax_cd, ax_cm = axes
    ax_cl.set_xlabel("AoA (deg)"); ax_cl.set_ylabel("CL"); ax_cl.set_title("CL vs AoA")
    ax_cd.set_xlabel("AoA (deg)"); ax_cd.set_ylabel("CD"); ax_cd.set_title("CD vs AoA")
    ax_cm.set_xlabel("AoA (deg)"); ax_cm.set_ylabel("Cm"); ax_cm.set_title("Cm vs AoA")

    for entry in entries:
        label = entry.get("Name", "")
        aoa_cl = [p["Time"]  for p in entry["ClVsAoA"]]
        cl     = [p["Value"] for p in entry["ClVsAoA"]]
        aoa_cd = [p["Time"]  for p in entry["CdVsAoA"]]
        cd     = [p["Value"] for p in entry["CdVsAoA"]]
        aoa_cm = [p["Time"]  for p in entry["CmVsAoA"]]
        cm     = [p["Value"] for p in entry["CmVsAoA"]]

        ax_cl.plot(aoa_cl, cl, linewidth=1.0, label=label)
        ax_cd.plot(aoa_cd, cd, linewidth=1.0, label=label)
        ax_cm.plot(aoa_cm, cm, linewidth=1.0, label=label)

    for ax in axes:
        ax.grid(True, linestyle="--", linewidth=0.5, alpha=0.55)
        ax.legend(fontsize=6, ncol=2)

    fig.tight_layout()
    return fig


if __name__ == "__main__":
    log("INFO", f"Visualising: {Path(PROFILE_PATH).stem}  hinge={HINGE_LOCATION}  "
                f"flap=[{MIN_FLAP}, {MAX_FLAP}]")

    geometry_info = []
    fig_geo = _geometry_figure(PROFILE_PATH, HINGE_LOCATION, MIN_FLAP, MAX_FLAP, geometry_info)

    polar_path = _find_polar_json(PROFILE_PATH, HINGE_LOCATION, MIN_FLAP, MAX_FLAP)
    if polar_path:
        log("INFO", f"Polar data found: {polar_path.name}")
        _polar_figure(polar_path)
    else:
        log("INFO", "No extrapolated polar JSON found — showing geometry only")

    plt.show()
    
    for i, geometry in enumerate(geometry_info): 
        generate_mesh(geometry[0], geometry[1], True)
