import subprocess
import pandas as pd
from pathlib import Path
from typing import Optional, Dict

from logger import log

_BASE_DIR    = Path(__file__).parent
_SU2_EXE     = str(_BASE_DIR / "SU2-v8.4.0-win64" / "bin" / "SU2_CFD.exe").replace("\\", "/")
_CONFIG_FILE = _BASE_DIR / "airfoil.cfg"
_HISTORY_CSV = _BASE_DIR / "history.csv"

_CONFIG_TEMPLATE = """\
% --- Solver ---
SOLVER= RANS
KIND_TURB_MODEL= SA
MATH_PROBLEM= DIRECT
RESTART_SOL= {restart}
SOLUTION_FILENAME= restart_flow
RESTART_FILENAME= restart_flow

% --- Flow ---
MACH_NUMBER= 0.15
AOA= {aoa}
REYNOLDS_NUMBER= 1000000
REF_LENGTH= 1.0
REF_AREA= 1.0

{boundaries_config}

% --- Numerics ---
CONV_NUM_METHOD_FLOW= ROE
MUSCL_FLOW= YES
SLOPE_LIMITER_FLOW= VENKATAKRISHNAN
VENKAT_LIMITER_COEFF= 0.1
ROE_KAPPA= 0.5
TIME_DISCRE_FLOW= EULER_IMPLICIT
CONV_NUM_METHOD_TURB= SCALAR_UPWIND
TIME_DISCRE_TURB= EULER_IMPLICIT
LOW_MACH_PREC= YES
LOW_MACH_CORR= YES

% --- Linear solver ---
LINEAR_SOLVER= FGMRES
LINEAR_SOLVER_PREC= ILU
LINEAR_SOLVER_ILU_FILL_IN= 0
LINEAR_SOLVER_ITER= 15
LINEAR_SOLVER_ERROR= 1E-6

% --- Convergence ---
CFL_NUMBER= 20.0
CFL_ADAPT= YES
CFL_ADAPT_PARAM= ( 0.5, 1.1, 1.0, 50.0 )
CFL_REDUCTION_TURB= 0.5
ITER= 2000
CONV_FIELD= RMS_DENSITY
CONV_RESIDUAL_MINVAL= {rms_quality}
CONV_STARTITER= 10

% --- Output ---
MESH_FILENAME= airfoil.su2
MESH_FORMAT= SU2
TABULAR_FORMAT= CSV
OUTPUT_FILES= (PARAVIEW, SURFACE_PARAVIEW)
VOLUME_FILENAME= flow
SURFACE_FILENAME= surface_flow
CONV_FILENAME= history
HISTORY_OUTPUT= (ITER, RMS_RES, AERO_COEFF)
"""


def run_su2(aoa: float, core_number: int, hinge_location: float, rms_quality: float, use_restart: bool = False) -> None:
    log("INFO", f"Running SU2: AoA={aoa:+.1f}deg  restart={use_restart}")
    if hinge_location != 0:
        boundaries = """% --- Boundaries ---
        MARKER_HEATFLUX= ( wing, 0.0, flap, 0.0 )
        MARKER_FAR= ( farfield )
        MARKER_MONITORING= ( wing, flap )"""
    else:
        boundaries = """% --- Boundaries ---
        MARKER_HEATFLUX= ( airfoil, 0.0 )
        MARKER_FAR= ( farfield )
        MARKER_MONITORING= ( airfoil )"""
    
    _CONFIG_FILE.write_text(
        _CONFIG_TEMPLATE.format(restart="YES" if use_restart else "NO", aoa=aoa, rms_quality=rms_quality, boundaries_config=boundaries),
        encoding="utf-8",
    )
    cmd = ["mpiexec.exe", "-n", str(core_number), _SU2_EXE, "airfoil.cfg"]
    try:
        subprocess.run(cmd, check=True, cwd=str(_BASE_DIR), timeout=900)
        log("INFO", f"SU2 complete: AoA={aoa:+.1f}deg")
    except subprocess.TimeoutExpired:
        log("ERROR", f"SU2 timeout: AoA={aoa:+.1f}deg")
        raise
    except Exception as e:
        log("ERROR", f"SU2 failed: AoA={aoa:+.1f}deg — {e}")
        raise


def read_su2_results() -> Optional[Dict]:
    if not _HISTORY_CSV.exists():
        log("ERROR", f"SU2 history not found: {_HISTORY_CSV}")
        return None
    df = pd.read_csv(_HISTORY_CSV, skipinitialspace=True)
    df.columns = df.columns.str.strip()
    if df.empty:
        log("WARNING", "SU2 history file is empty")
        return None
    row = df.iloc[-1]
    return {
        "rms_rho": float(row["rms[Rho]"]),
        "CL":      float(row["CL"]),
        "CD":      float(row["CD"]),
        "CMz":     float(row["CMz"]),
    }
