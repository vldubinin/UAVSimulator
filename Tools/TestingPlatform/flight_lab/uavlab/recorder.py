"""
Запис сесії в sessions/<session_id>/:

  manifest.json      — умови тесту з Unreal (UTestSessionSubsystem)
  config.yaml        — конфіг тесту
  controller_src/    — копія коду алгоритму (файл + сусідні .py)
  truth.csv          — flight_truth на КОЖНОМУ пакеті шини (сплощені колонки)
  steps.csv          — кожен крок алгоритму: команда, латентність, ctx.log(...)
  sensors.jsonl      — сирі JSON-payload'и інших сенсорів (обмежено sensors_max_hz)
  events.jsonl       — події сесії (Unreal + стенд + ctx.event)
  summary.json       — підсумок прогону (тривалість, причина зупинки, лічильники)
"""

from __future__ import annotations

import json
import re
import shutil
import time
from pathlib import Path
from typing import Any, Optional

import pandas as pd
import yaml


def flatten(d: dict, prefix: str = "") -> dict:
    out: dict[str, Any] = {}
    for k, v in d.items():
        key = f"{prefix}{k}"
        if isinstance(v, dict):
            out.update(flatten(v, key + "."))
        elif isinstance(v, (list, tuple)):
            out[key] = json.dumps(v)
        else:
            out[key] = v
    return out


class SessionRecorder:
    def __init__(self, root: Path, session_id: str, config: dict, controller_path: Path,
                 sensors_max_hz: float = 10.0, record_sensors: bool = True,
                 extra_sources: Optional[list] = None) -> None:
        self.dir = Path(root) / session_id
        self.dir.mkdir(parents=True, exist_ok=True)
        self.session_id = session_id
        self._truth_rows: list[dict] = []
        self._step_rows: list[dict] = []
        self._events_f = (self.dir / "events.jsonl").open("a", encoding="utf-8")
        self._sensors_f = (self.dir / "sensors.jsonl").open("a", encoding="utf-8") if record_sensors else None
        self._sensors_min_dt = 1.0 / sensors_max_hz if sensors_max_hz and sensors_max_hz > 0 else 0.0
        self._last_sensor_write: dict[str, float] = {}

        cfg_to_save = {k: v for k, v in config.items() if not k.startswith("_")}
        (self.dir / "config.yaml").write_text(yaml.safe_dump(cfg_to_save, allow_unicode=True, sort_keys=False),
                                              encoding="utf-8")
        self._copy_source(controller_path, extra_sources or [])

    def _copy_source(self, controller_path: Path, extra_sources: list) -> None:
        dst = self.dir / "controller_src"
        dst.mkdir(exist_ok=True)
        controller_path = Path(controller_path).resolve()
        shutil.copy2(controller_path, dst / controller_path.name)
        # Сусідні модулі — лише ті, що алгоритм імпортує (import x / from x import ...).
        imported = set(re.findall(r"^\s*(?:from|import)\s+([A-Za-z_]\w*)",
                                  controller_path.read_text(encoding="utf-8", errors="replace"), re.M))
        for sibling in controller_path.parent.glob("*.py"):
            if sibling != controller_path and sibling.stem in imported and sibling.stat().st_size < 200_000:
                shutil.copy2(sibling, dst / sibling.name)
        # Модулі алгоритму з інших тек (record.extra_sources) — щоб LLM бачив увесь код.
        for extra in extra_sources:
            p = Path(extra)
            if not p.is_absolute():
                p = controller_path.parent / p
            for f in ([p] if p.is_file() else sorted(p.glob("*.py")) if p.is_dir() else []):
                shutil.copy2(f, dst / f.name)
        (dst / "ENTRY").write_text(controller_path.name, encoding="utf-8")

    # ── запис ──────────────────────────────────────────────────────────────
    def write_manifest(self, manifest: dict) -> None:
        (self.dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")

    def add_truth(self, bus_time: float, wall_time: float, truth: dict) -> None:
        row = {"bus_time": bus_time, "wall_time": wall_time}
        row.update(flatten(truth))
        self._truth_rows.append(row)

    def add_sensor(self, topic: str, sim_time: float, payload: Any) -> None:
        if self._sensors_f is None:
            return
        last = self._last_sensor_write.get(topic, -1e9)
        if sim_time - last < self._sensors_min_dt:
            return
        self._last_sensor_write[topic] = sim_time
        self._sensors_f.write(json.dumps({"t": sim_time, "topic": topic, "data": payload}, ensure_ascii=False) + "\n")

    def add_step(self, row: dict) -> None:
        self._step_rows.append(row)

    def add_event(self, event: dict) -> None:
        event = {"wall_time": time.time(), **event}
        self._events_f.write(json.dumps(event, ensure_ascii=False, default=str) + "\n")
        self._events_f.flush()

    # ── фіналізація ────────────────────────────────────────────────────────
    def finalize(self, summary: dict) -> None:
        if self._truth_rows:
            pd.DataFrame(self._truth_rows).to_csv(self.dir / "truth.csv", index=False)
        if self._step_rows:
            pd.DataFrame(self._step_rows).to_csv(self.dir / "steps.csv", index=False)
        (self.dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2, default=str),
                                               encoding="utf-8")
        self._events_f.close()
        if self._sensors_f:
            self._sensors_f.close()

    @property
    def truth_count(self) -> int:
        return len(self._truth_rows)

    @property
    def step_count(self) -> int:
        return len(self._step_rows)


def load_session(session_dir: str | Path) -> dict:
    """Читає записану сесію для аналізу (без Unreal)."""
    d = Path(session_dir)

    def _read_json(name: str) -> Optional[dict]:
        p = d / name
        return json.loads(p.read_text(encoding="utf-8")) if p.exists() else None

    def _read_jsonl(name: str) -> list[dict]:
        p = d / name
        if not p.exists():
            return []
        return [json.loads(line) for line in p.read_text(encoding="utf-8").splitlines() if line.strip()]

    src_dir = d / "controller_src"
    entry = (src_dir / "ENTRY").read_text(encoding="utf-8").strip() if (src_dir / "ENTRY").exists() else None
    sources: dict[str, str] = {}
    if src_dir.exists():
        for p in sorted(src_dir.glob("*.py")):
            sources[p.name] = p.read_text(encoding="utf-8", errors="replace")

    return {
        "dir": d,
        "manifest": _read_json("manifest.json") or {},
        "summary": _read_json("summary.json") or {},
        "config": yaml.safe_load((d / "config.yaml").read_text(encoding="utf-8")) if (d / "config.yaml").exists() else {},
        "truth": pd.read_csv(d / "truth.csv") if (d / "truth.csv").exists() else pd.DataFrame(),
        "steps": pd.read_csv(d / "steps.csv") if (d / "steps.csv").exists() else pd.DataFrame(),
        "events": _read_jsonl("events.jsonl"),
        "sources": sources,
        "entry": entry,
    }
