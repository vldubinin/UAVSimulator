import subprocess
import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
_REPO_ROOT   = _SCRIPT_DIR.parent.parent

# ---------------------------------------------------------------------------
# Default parameters for a standalone (no Unreal Editor) polar sweep run.
# Adjust these to match the target airfoil / surface before running.
# ---------------------------------------------------------------------------

PARAMETERS =  ['C:/Users/DubakusPC/Documents/Unreal Projects/UAVSimulator/Content/WingProfile/NACA_0009/naca0009.dat', 'TailHorizontal', '6', '0.8', '-45', '45', '1', '-6', 'true']

if __name__ == "__main__":
    target = _SCRIPT_DIR / "execute_su2_calculation.py"
    args = [sys.executable, str(target)] + PARAMETERS
    result = subprocess.run(args)
    sys.exit(result.returncode)
