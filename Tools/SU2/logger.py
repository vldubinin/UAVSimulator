from datetime import datetime
from pathlib import Path

_LOGS_DIR = Path(__file__).parent / "logs"


def log(level: str, message: str) -> None:
    _LOGS_DIR.mkdir(exist_ok=True)
    now = datetime.now()
    line = f"[{now.strftime('%Y-%m-%d %H:%M:%S')}] [{level}] {message}"
    print(line)
    log_file = _LOGS_DIR / f"log_{now.day}_{now.month}_{now.year}.log"
    with log_file.open("a", encoding="utf-8") as f:
        f.write(line + "\n")
