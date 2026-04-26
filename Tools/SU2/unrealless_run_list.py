import subprocess
import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
_REPO_ROOT   = _SCRIPT_DIR.parent.parent

# ---------------------------------------------------------------------------
# Default parameters for a standalone (no Unreal Editor) polar sweep run.
# Adjust these to match the target airfoil / surface before running.
# ---------------------------------------------------------------------------
ANGLES = [9.0, 21.0, 25.0]

if __name__ == "__main__":
    target = _SCRIPT_DIR / "execute_su2_calculation.py"
    
    for angle in ANGLES:
        print(f"\n---> Запуск розрахунку SU2 для кута закрилка: {angle}°")
        
        # Обов'язково конвертуємо float у str для subprocess
        str_angle = str(angle)
        
        PARAMETERS = [
            "C:/Users/DubakusPC/Documents/Unreal Projects/UAVSimulator/Content/WingProfile/NACA_2412/naca2412.dat", 
            "Name", "4", "0.8", str_angle, str_angle, "1", "-4", "true"
        ]
        
        args = [sys.executable, str(target)] + PARAMETERS
        
        try:
            result = subprocess.run(args)
            
            if result.returncode != 0:
                print(f"[УВАГА] Розрахунок для кута {angle}° завершився з помилкою (код: {result.returncode})")
            else:
                print(f"[ОК] Розрахунок для кута {angle}° успішно завершено.")
                
        except Exception as e:
            print(f"[ПОМИЛКА] Не вдалося запустити процес для кута {angle}°: {e}")