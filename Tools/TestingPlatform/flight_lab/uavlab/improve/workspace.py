"""
Версіонований робочий простір `uavlab improve`:

  improve/<run_id>/
    origin.json          — відповідність файлів версії оригіналам (для `uavlab apply`)
    acceptance.yaml      — заморожені критерії приймання
    v0/ v1/ ...          — версії: src/ (код алгоритму), config.yaml, sessions/, change.json
    summary.json, improve_report.html

Оригінальні файли не змінюються ніколи — лише `uavlab apply` після вашого рішення.
"""

from __future__ import annotations

import copy
import json
import re
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import yaml


@dataclass
class Version:
    index: int
    dir: Path
    entry: str                      # ім'я головного файлу алгоритму в src/
    config: dict
    parent: Optional[int] = None
    change: dict = field(default_factory=dict)   # що змінив агент
    session_dir: Optional[Path] = None
    evaluation: Optional[dict] = None
    report: Optional[dict] = None
    kept: Optional[bool] = None
    status: str = "created"         # created | rejected | run | failed_to_run

    @property
    def src(self) -> Path:
        return self.dir / "src"

    @property
    def entry_path(self) -> Path:
        return self.src / self.entry

    @property
    def config_path(self) -> Path:
        return self.dir / "config.yaml"

    def save_config(self) -> None:
        cfg = {k: v for k, v in self.config.items() if not k.startswith("_")}
        self.config_path.write_text(yaml.safe_dump(cfg, allow_unicode=True, sort_keys=False), encoding="utf-8")

    def save_change(self) -> None:
        (self.dir / "change.json").write_text(json.dumps(self.change, ensure_ascii=False, indent=2), encoding="utf-8")

    def sources(self) -> dict[str, str]:
        return {p.name: p.read_text(encoding="utf-8", errors="replace") for p in sorted(self.src.glob("*.py"))}


def _imported_siblings(controller: Path) -> list[Path]:
    text = controller.read_text(encoding="utf-8", errors="replace")
    names = set(re.findall(r"^\s*(?:from|import)\s+([A-Za-z_]\w*)", text, re.M))
    return [p for p in controller.parent.glob("*.py") if p != controller and p.stem in names]


class Workspace:
    def __init__(self, run_dir: Path) -> None:
        self.run_dir = run_dir
        self.versions: list[Version] = []
        self.origin: dict[str, str] = {}

    @classmethod
    def create(cls, run_dir: Path, controller_path: Path, cfg: dict) -> "Workspace":
        ws = cls(run_dir)
        run_dir.mkdir(parents=True, exist_ok=False)
        v0 = Version(0, run_dir / "v0", controller_path.name, copy.deepcopy(cfg))
        v0.src.mkdir(parents=True)

        files = [controller_path] + _imported_siblings(controller_path)
        for extra in cfg.get("record", {}).get("extra_sources") or []:
            p = Path(extra)
            p = p if p.is_absolute() else (controller_path.parent / p)
            files += [p] if p.is_file() else sorted(p.glob("*.py")) if p.is_dir() else []
        for f in files:
            f = f.resolve()
            shutil.copy2(f, v0.src / f.name)
            ws.origin[f.name] = str(f)

        # Копії вже лежать поруч у src/ — extra_sources більше не потрібні; сесії — всередині версії.
        v0.config.setdefault("record", {})["extra_sources"] = []
        v0.config["record"]["sessions_dir"] = str((v0.dir / "sessions").resolve())
        v0.config["analysis"]["enabled"] = False  # аналіз запускає improve сам
        v0.save_config()
        ws.versions.append(v0)
        (run_dir / "origin.json").write_text(json.dumps(
            {"controller": str(controller_path.resolve()), "config": cfg.get("_config_path"), "files": ws.origin},
            ensure_ascii=False, indent=2), encoding="utf-8")
        return ws

    def branch(self, parent: Version) -> Version:
        idx = len(self.versions)
        v = Version(idx, self.run_dir / f"v{idx}", parent.entry, copy.deepcopy(parent.config), parent=parent.index)
        shutil.copytree(parent.src, v.src, ignore=shutil.ignore_patterns("__pycache__"))
        v.config["record"]["sessions_dir"] = str((v.dir / "sessions").resolve())
        v.save_config()
        self.versions.append(v)
        return v
