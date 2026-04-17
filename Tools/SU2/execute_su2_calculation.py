import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR))
sys.path.insert(0, str(_SCRIPT_DIR.parent / "PyInstall"))

from logger import log
from geometry import load_airfoil_dat
from sweep import SweepConfig, history_path, load_history, build_completed_from_meta, run_polar_sweep, extrapolate_and_save
from su2_unreal_import import create_unreal_assets
from visualise_geometry import visualise_geometry

# ---------------------------------------------------------------------------
# Usage:
#   python execute_su2_calculation.py <profile_path> <surface_name> <cores>
#       <hinge> <min_flap> <max_flap> <flap_step> <rms_quality> <resume>
#       [re] [cd_max]
#
# Example:
#   python execute_su2_calculation.py \
#       "C:/.../Content/WingProfile/NACA_0009/naca0009.dat" \
#       "Wing" 4 0.75 -40.0 40.0 1.0 -6 true
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if len(sys.argv) < 10:
        print(
            "Usage: execute_su2_calculation.py "
            "<profile_path> <surface_name> <cores> "
            "<hinge> <min_flap> <max_flap> <flap_step> <rms_quality> <resume> "
            "[re=750000.0] [cd_max=1.25]"
        )
        sys.exit(1)

    profile_path   = sys.argv[1]
    surface_name   = sys.argv[2]
    core_number    = int(sys.argv[3])
    hinge_location = float(sys.argv[4])
    min_flap       = float(sys.argv[5])
    max_flap       = float(sys.argv[6])
    flap_step      = float(sys.argv[7])
    rms_quality    = float(sys.argv[8])
    resume         = sys.argv[9].strip().lower() in ("true", "1", "yes")
    re             = float(sys.argv[10]) if len(sys.argv) > 10 else 750000.0
    cd_max         = float(sys.argv[11]) if len(sys.argv) > 11 else 1.25

    cfg = SweepConfig(
        profile_path   = profile_path,
        surface_name   = surface_name,
        core_number    = core_number,
        hinge_location = hinge_location,
        min_flap       = min_flap,
        max_flap       = max_flap,
        flap_step      = flap_step,
        rms_quality    = rms_quality,
        resume         = resume,
        re             = re,
        cd_max         = cd_max,
    )

    log("INFO", f"Starting polar sweep. Arguments: {sys.argv[1:]}")

    base_coords = load_airfoil_dat(cfg.profile_path)
    visualise_geometry(base_coords, cfg)
    hist_path   = history_path(cfg)

    if cfg.resume:
        polar, meta = load_history(hist_path)
        if meta is not None:
            log("INFO", f"Resuming: last flap={meta['last_flap']:.1f}deg AoA={meta['last_aoa']:.1f}deg "
                        f"status={meta['status']} — {hist_path.name}")
            completed = build_completed_from_meta(cfg, meta["last_flap"], meta["last_aoa"])
            log("INFO", f"{len(completed)} conditions will be skipped")
        else:
            completed = set()
            log("INFO", f"No resume metadata found — fresh start — {hist_path.name}")
    else:
        polar, completed = {}, set()
        log("INFO", f"Fresh start — {hist_path.name}")

    polar     = run_polar_sweep(base_coords, cfg, polar, completed)
    json_path = extrapolate_and_save(polar, cfg)

    create_unreal_assets(
        extrapolated_json_path = str(json_path),
        profile_path           = cfg.profile_path,
        hinge_location         = cfg.hinge_location,
        min_flap               = cfg.min_flap,
        max_flap               = cfg.max_flap,
    )

    log("INFO", f"Polar sweep finished: {cfg.surface_name}")
