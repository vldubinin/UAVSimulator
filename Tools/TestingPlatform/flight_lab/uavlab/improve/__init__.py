"""
uavlab improve — замкнений цикл «тест → аналіз → правка LLM → повторний тест» (варіант A, Claude API).

    uavlab improve my_algo.py -c test.yaml [--max-iterations 5] [--max-cost 5] [--approve] [--allow params,code,autopilot_pid]
    uavlab improve --resume improve/<run_id> [--max-iterations N] [--max-cost USD]   # продовжити перерваний
    uavlab apply improve/<run_id>          # перенести найкращу версію в оригінальні файли

Алгоритм:
  v0 = копія оригіналу -> тест -> аналіз -> оцінка за критеріями приймання (якщо їх немає в конфігу —
  Claude один раз перетворює objective на формальні критерії, вони заморожуються).
  Далі, поки не пройдено / не вичерпано ітерації або бюджет:
    нова версія = копія НАЙКРАЩОЇ -> агент вносить правки -> (--approve: підтвердження) -> тест -> аналіз
    -> оцінка; краща — стає найкращою, інакше відкидається (агент бачить це в історії).
  Наприкінці — валідація найкращої версії на improve.validation_configs і звіт improve_report.html.
Кожен тест — окремий процес `uavlab run` (чисті імпорти модулів алгоритму, ізоляція падінь).
Стан зберігається після кожного кроку (state.json) — перерваний прогін (Ctrl+C, падіння Unreal, помилка
API) продовжується з останнього завершеного етапу: `uavlab improve --resume <тека прогону>`.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

import yaml

from ..config import load_config
from .acceptance import evaluate, generate_criteria, is_better
from .agent import ChangeAgent, usage_cost
from .report import render_improve_report
from .workspace import Version, Workspace


def _log(msg: str) -> None:
    print(f"[uavlab improve] {msg}", flush=True)


class Budget:
    def __init__(self, max_usd: float) -> None:
        self.max_usd = max_usd
        self.items: list[tuple[str, float]] = []

    def add(self, what: str, usd: float) -> None:
        self.items.append((what, usd))

    @property
    def spent(self) -> float:
        return sum(c for _, c in self.items)

    @property
    def exhausted(self) -> bool:
        return self.spent >= self.max_usd


def _run_version(controller: Path, config_path: Path, sessions_dir: Path, extra_path: str, log_path: Path) -> Optional[Path]:
    """Запускає `uavlab run` в окремому процесі; повертає теку створеної сесії."""
    before = set(sessions_dir.glob("*")) if sessions_dir.exists() else set()
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(filter(None, [extra_path, env.get("PYTHONPATH", "")]))
    env["PYTHONIOENCODING"] = "utf-8"
    cmd = [sys.executable, "-m", "uavlab", "run", str(controller), "-c", str(config_path), "--no-analysis"]
    with log_path.open("w", encoding="utf-8") as log:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                encoding="utf-8", errors="replace", env=env, cwd=str(controller.parent))
        for line in proc.stdout:
            log.write(line)
            if "t=" not in line:  # покрокові рядки телеметрії — лише в лог
                print("    " + line.rstrip(), flush=True)
        proc.wait()
    created = sorted(set(sessions_dir.glob("*")) - before, key=lambda p: p.stat().st_mtime) if sessions_dir.exists() else []
    if proc.returncode != 0 or not created:
        return None
    return created[-1]


def _analyze(session_dir: Path, analysis_cfg: dict, use_llm: bool, budget: Budget) -> tuple[dict, Optional[dict]]:
    from ..analysis import analyze_session

    analyze_session(session_dir, analysis_cfg, use_llm=use_llm)
    metrics = json.loads((session_dir / "metrics.json").read_text(encoding="utf-8"))
    report = None
    if (session_dir / "report.json").exists():
        report = json.loads((session_dir / "report.json").read_text(encoding="utf-8"))
        if report.get("_meta"):
            budget.add(f"analysis {session_dir.name}", usage_cost(report["_meta"]))
    return metrics, report


def _effective_pids(manifest: dict) -> dict:
    out = {}
    for a in manifest.get("aircraft", []):
        ap = a.get("autopilot") or {}
        if not ap.get("active"):
            continue
        for axis in ("roll", "pitch", "yaw_rate"):
            pid = ap.get(f"{axis}_pid") or {}
            out[axis] = {k: pid.get(k) for k in ("kp", "ki", "kd", "integralClamp", "derivativeFilterTimeConstant",
                                                 "bInvertOutput", "bDerivativeOnMeasurement", "outputMin", "outputMax")}
            out[axis]["gain_schedule_active"] = (ap.get("gain_schedule_active") or {}).get(axis)
    return out


def _evaluate_version(v: Version, criteria: list[dict], metrics: dict) -> dict:
    summary = json.loads((v.session_dir / "summary.json").read_text(encoding="utf-8"))
    return evaluate(criteria, metrics, summary)


def _short_report(report: Optional[dict]) -> Optional[dict]:
    if not report:
        return None
    return {k: report.get(k) for k in ("verdict", "summary", "objective_assessment", "issues", "caveats")}


def _context(ws: Workspace, best: Version, last: Optional[Version], criteria: list[dict], cfg: dict,
             history: list[dict], best_metrics: dict) -> list[tuple[str, str]]:
    manifest = json.loads((best.session_dir / "manifest.json").read_text(encoding="utf-8")) if best.session_dir else {}
    metrics = dict(best_metrics)
    metrics["anomalies"] = metrics.get("anomalies", [])[:20]
    blocks = [
        ("test", json.dumps({"name": cfg.get("name"), "objective": cfg.get("objective"),
                             "stop_criteria_readonly": cfg.get("stop"), "simulator_mode": cfg.get("mode")},
                            ensure_ascii=False, indent=1)),
        ("permissions", json.dumps({"allowed_changes": cfg["improve"].get("allow")}, ensure_ascii=False)),
        ("acceptance_criteria_frozen", json.dumps(criteria, ensure_ascii=False, indent=1)),
        ("current_best", json.dumps({
            "version": f"v{best.index}",
            "evaluation": best.evaluation,
            "controller_params": best.config.get("controller_params"),
            "autopilot_pid_overrides_in_config": best.config.get("autopilot_pid"),
            "effective_autopilot_pid_from_manifest": _effective_pids(manifest),
            "gain_schedule_multiplier_stats": best_metrics.get("autopilot_gain_schedule_multiplier"),
        }, ensure_ascii=False, indent=1, default=str)),
        ("best_analysis_report", json.dumps(_short_report(best.report), ensure_ascii=False, indent=1)),
        ("best_metrics", json.dumps(metrics, ensure_ascii=False, indent=1, default=str)),
        ("iteration_history", json.dumps(history, ensure_ascii=False, indent=1, default=str)),
    ]
    if last is not None and last is not best and last.status == "scored":
        blocks.append(("last_attempt_reverted", json.dumps({
            "version": f"v{last.index}", "change": {k: last.change.get(k) for k in ("summary", "hypothesis", "params", "autopilot_pid")},
            "diff": last.change.get("diff"), "evaluation": last.evaluation,
            "analysis_issues": [(i.get("severity"), i.get("title")) for i in (last.report or {}).get("issues", [])],
        }, ensure_ascii=False, indent=1, default=str)))
    src = best.sources()
    entry_text = "\n".join(f"{i:4d}| {l}" for i, l in enumerate(src.get(best.entry, "").splitlines(), 1))
    blocks.append(("sources", f"files: {', '.join(f'{n} ({len(t)} chars)' for n, t in src.items())}\n"
                              f"entry file {best.entry}:\n{entry_text}"))
    return blocks


def _confirm(v: Version) -> bool:
    print("\n" + "=" * 70)
    print(f"Пропозиція для v{v.index}: {v.change.get('summary')}")
    print(f"Гіпотеза: {v.change.get('hypothesis')}")
    if v.change.get("params"):
        print("controller_params:", json.dumps(v.change["params"], ensure_ascii=False))
    if v.change.get("autopilot_pid"):
        print("autopilot_pid:", json.dumps(v.change["autopilot_pid"], ensure_ascii=False))
    if v.change.get("diff"):
        print(v.change["diff"])
    print("=" * 70)
    return input("Запустити тест з цими змінами? [y/N] ").strip().lower() in ("y", "yes", "т", "так")


class ImproveInterrupted(Exception):
    """Цикл зупинено так, що його можна продовжити через --resume (Ctrl+C, помилка API агента...)."""


class ImproveRun:
    """Стан циклу improve + його збереження після КОЖНОГО кроку (state.json), щоб `--resume`
    продовжив з останнього завершеного етапу:

      версія created            -> перервано під час роботи агента: версію прибрано, правки готуються заново
      версія proposed/approved  -> правки збережено, тест не завершено: тест запускається повторно (без агента)
      версія tested             -> тест завершено, оцінки немає: лише аналіз + оцінка
      версія scored/rejected/…  -> готово
      phase validation          -> цикл завершено, валідація/звіт не дійшли до кінця
    """

    def __init__(self, run_dir: Path, cfg: dict, ws: Workspace, state: dict) -> None:
        self.run_dir = run_dir
        self.cfg = cfg
        self.icfg = cfg["improve"]
        self.ws = ws
        self.analysis_cfg = dict(cfg["analysis"])
        self.extra_path = str(Path(state["controller"]).parent)  # нескопійовані локальні залежності алгоритму
        self.budget = Budget(float(self.icfg.get("max_cost_usd", 5.0)))
        self.budget.items = [tuple(x) for x in state.get("budget_items", [])]
        self.history: list[dict] = state.get("history", [])
        self.criteria: list[dict] = state.get("criteria") or list(cfg.get("acceptance") or [])
        self.criteria_notes: str = state.get("criteria_notes", "")
        self.iterations_done: int = state.get("iterations_done", 0)
        self.best_index: Optional[int] = state.get("best")
        self.last_index: Optional[int] = state.get("last")
        self.phase: str = state.get("phase", "loop")
        self.stop_reason: Optional[str] = state.get("stop_reason")
        self.validation: list[dict] = state.get("validation", [])
        self.state = state
        self.resumed = False
        self._agent: Optional[ChangeAgent] = None

    # ── створення / відновлення ──────────────────────────────────────────
    @classmethod
    def create(cls, controller_path: Path, cfg: dict) -> "ImproveRun":
        controller_path = controller_path.resolve()
        run_dir = (Path(cfg["improve"].get("runs_dir", "improve")).resolve()
                   / f"{time.strftime('%Y%m%d_%H%M%S')}_{controller_path.stem}")
        ws = Workspace.create(run_dir, controller_path, cfg)
        (run_dir / "run_config.yaml").write_text(
            yaml.safe_dump({k: v for k, v in cfg.items() if not k.startswith("_")} | {"_config_path": cfg.get("_config_path")},
                           allow_unicode=True, sort_keys=False), encoding="utf-8")
        run = cls(run_dir, cfg, ws, {"controller": str(controller_path)})
        run.save()
        return run

    @classmethod
    def resume(cls, run_dir: Path, improve_overrides: dict) -> "ImproveRun":
        run_dir = run_dir.resolve()
        if not (run_dir / "state.json").exists():
            raise SystemExit(f"[uavlab improve] {run_dir} не схожий на теку прогону improve (немає state.json).")
        state = json.loads((run_dir / "state.json").read_text(encoding="utf-8"))
        cfg = load_config(None, yaml.safe_load((run_dir / "run_config.yaml").read_text(encoding="utf-8")))
        cfg["improve"].update(improve_overrides)
        ws = Workspace.load(run_dir, state.get("versions", []))
        run = cls(run_dir, cfg, ws, state)
        run.resumed = True
        if run.phase == "done":
            # Завершений прогін можна продовжити, лише якщо дали більше ітерацій/бюджету і ще не все пройдено.
            best = run.best
            if best is not None and best.evaluation and best.evaluation.get("all_passed"):
                _log("Усі критерії вже пройдено — продовжувати нічого, лише оновлюю звіт.")
                run.phase = "report"
            else:
                run.phase, run.stop_reason = "loop", None
        return run

    # ── стан ─────────────────────────────────────────────────────────────
    def version(self, index: Optional[int]) -> Optional[Version]:
        return next((v for v in self.ws.versions if v.index == index), None) if index is not None else None

    @property
    def best(self) -> Optional[Version]:
        return self.version(self.best_index)

    def save(self) -> None:
        self.state.update({
            "versions": [v.to_state() for v in self.ws.versions],
            "best": self.best_index, "last": self.last_index, "iterations_done": self.iterations_done,
            "history": self.history, "criteria": self.criteria, "criteria_notes": self.criteria_notes,
            "budget_items": self.budget.items, "phase": self.phase, "stop_reason": self.stop_reason,
            "validation": self.validation, "updated": time.strftime("%Y-%m-%d %H:%M:%S"),
        })
        tmp = self.run_dir / "state.json.tmp"
        tmp.write_text(json.dumps(self.state, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
        os.replace(tmp, self.run_dir / "state.json")  # атомарно: перерваний запис не зіпсує стан

    @property
    def agent(self) -> ChangeAgent:
        if self._agent is None:
            self._agent = ChangeAgent(self.icfg, language=self.analysis_cfg.get("language", "uk"))
        return self._agent

    # ── кроки ────────────────────────────────────────────────────────────
    def _test(self, v: Version) -> None:
        _log(f"Тест версії v{v.index}...")
        session = _run_version(v.entry_path, v.config_path, Path(v.config["record"]["sessions_dir"]),
                               self.extra_path, v.dir / "run.log")
        if session is None:
            # Сесії немає — зазвичай це середовище (Unreal не відповідає / впав), а не версія алгоритму
            # (винятки алгоритму дають сесію зі stop_reason=controller_exception). Версія лишається
            # непротестованою, --resume запустить її тест повторно.
            self.save()
            raise ImproveInterrupted(f"тест v{v.index} не відбувся — перевірте Unreal (лог: {v.dir / 'run.log'})")
        summary = json.loads((session / "summary.json").read_text(encoding="utf-8")) if (session / "summary.json").exists() else {}
        if summary.get("stop_reason") == "user_interrupt":
            # Ctrl+C під час тесту — це не результат версії: лишаємо її непротестованою.
            raise ImproveInterrupted(f"тест v{v.index} перервано")
        v.session_dir, v.status = session, "tested"
        self.save()

    def _score(self, v: Version) -> None:
        metrics, v.report = _analyze(v.session_dir, self.analysis_cfg,
                                     bool(self.icfg.get("analyze_each_iteration", True)), self.budget)
        if not self.criteria:
            _log("Критеріїв приймання немає — генерую з objective (один раз, далі вони заморожені)...")
            self.criteria, self.criteria_notes, usage = generate_criteria(self.cfg.get("objective", ""), metrics, self.icfg)
            self.budget.add("acceptance criteria", usage_cost(usage))
            if not self.criteria:
                raise SystemExit("[uavlab improve] Не вдалося сформулювати критерії — задайте acceptance у конфігу.")
            for c in self.criteria:
                op = "max" if "max" in c else "min"
                _log(f"  критерій: {c['metric']} {'<=' if op == 'max' else '>='} {c[op]}  ({c.get('rationale', '')})")
            if self.criteria_notes:
                _log(f"  примітка: {self.criteria_notes}")
            if self.icfg.get("approve") and input("Прийняти ці критерії? [y/N] ").strip().lower() not in ("y", "yes", "т", "так"):
                self.criteria = []
                raise SystemExit("[uavlab improve] Критерії не прийнято — задайте acceptance у конфігу вручну.")
        (self.run_dir / "acceptance.yaml").write_text(
            yaml.safe_dump(self.criteria, allow_unicode=True, sort_keys=False), encoding="utf-8")
        v.evaluation = _evaluate_version(v, self.criteria, metrics)
        v.status = "scored"
        e = v.evaluation
        _log(f"v{v.index}: пройдено {e['passed']}/{e['total']} критеріїв, перевищення {e['violation']}"
             + (f", ПРОВАЛ: {e['hard_fail']}" if e["hard_fail"] else ""))

    def _decide(self, v: Version) -> None:
        """Порівняння з найкращою, запис в історію, оновлення best."""
        best = self.best
        ok = v.status == "scored"
        v.kept = bool(ok and (best is None or is_better(v.evaluation, best.evaluation)))
        if v.index == 0:
            v.kept = ok
        else:
            self.history.append({
                "version": f"v{v.index}", "base": f"v{v.parent}",
                "result": "kept (new best)" if v.kept else ("failed to run" if not ok else "reverted (not better)"),
                "summary": v.change.get("summary"), "hypothesis": v.change.get("hypothesis"),
                "params": v.change.get("params"), "autopilot_pid": v.change.get("autopilot_pid"),
                "code_edits": v.change.get("code_edits"),
                "evaluation": {k: (v.evaluation or {}).get(k) for k in ("passed", "total", "violation", "hard_fail")},
                "criteria_values": [(d["metric"], d["value"], d["ok"]) for d in (v.evaluation or {}).get("details", [])],
            })
            _log(f"  -> {self.history[-1]['result']}")
            self.last_index = v.index
        if v.kept:
            self.best_index = v.index
        self.save()

    def _finish_version(self, v: Version) -> None:
        """Доводить версію до кінця з того стану, на якому її перервали."""
        if v.status in ("proposed", "approved"):
            if v.index > 0:
                time.sleep(float(self.icfg.get("pause_between_runs_s", 3.0)))
            self._test(v)
        if v.status == "tested":
            if not (v.session_dir and (v.session_dir / "summary.json").exists()):
                v.status, v.session_dir = "proposed", None  # сесія не дописана — тест заново
                self.save()
                return self._finish_version(v)
            self._score(v)
            self._decide(v)

    def _recover(self) -> None:
        """Наводить лад після переривання: недороблені версії або доробляються, або прибираються."""
        for v in list(self.ws.versions):
            if v.status == "created" and v.index > 0:
                _log(f"v{v.index}: перервано під час роботи агента — версію прибрано, правки буде підготовлено заново")
                self.ws.discard(v)
                self.iterations_done = max(0, self.iterations_done - 1)
                self.save()
        for v in self.ws.versions:
            if v.status == "failed_to_run":  # стан зі старих прогонів — тест повторюється
                v.status = "proposed"
        v0 = self.version(0)
        if v0 is not None and v0.status == "created":
            v0.status = "proposed"  # базова версія не потребує агента — просто тест
        for v in list(self.ws.versions):
            if v.status in ("proposed", "approved", "tested"):
                if v.status != "tested" and self.icfg.get("approve") and v.index > 0 and v.status != "approved":
                    if not _confirm(v):
                        v.status, v.kept = "rejected", False
                        self.history.append({"version": f"v{v.index}", "result": "rejected by user",
                                             "summary": v.change.get("summary")})
                        self.last_index = v.index
                        self.save()
                        continue
                    v.status = "approved"
                    self.save()
                if self.resumed:
                    _log(f"Відновлення: доводжу v{v.index} (стан: {v.status})")
                self._finish_version(v)

    def _iteration(self) -> bool:
        """Одна ітерація. False — цикл треба зупинити (stop_reason виставлено)."""
        best = self.best
        if best.evaluation["all_passed"]:
            self.stop_reason = "all_criteria_passed"
            return False
        if self.iterations_done >= int(self.icfg.get("max_iterations", 5)):
            self.stop_reason = "max_iterations"
            return False
        if self.budget.spent >= self.budget.max_usd:
            self.stop_reason = "budget_exhausted"
            return False

        v = self.ws.branch(best)
        self.iterations_done += 1
        self.save()  # v зі статусом created: якщо агента переривуть — версію буде прибрано при --resume
        _log(f"Ітерація {self.iterations_done}: агент готує правки (база v{best.index})...")
        n_usage = len(self.agent.usages)
        last = self.version(self.last_index)
        try:
            v.change = self.agent.propose(
                v, _context(self.ws, best, last, self.criteria, self.cfg, self.history, best.metrics()), self.extra_path)
        except KeyboardInterrupt:
            raise
        except Exception as exc:
            raise ImproveInterrupted(f"агент не зміг запропонувати правки: {exc}") from exc
        finally:
            spent = sum(usage_cost(u) for u in self.agent.usages[n_usage:])
            if spent:
                self.budget.add(f"agent v{v.index}", spent)
        v.save_change()
        _log(f"  {v.change.get('summary', '')}")

        no_changes = not (v.change.get("diff") or v.change.get("params") or v.change.get("autopilot_pid"))
        if no_changes:
            v.status = "no_changes"
            self.stop_reason = "agent_stopped" if v.change.get("stop_improving") else "no_changes"
            self.history.append({"version": f"v{v.index}", "result": "no changes proposed", "summary": v.change.get("summary")})
            self.save()
            return False

        v.status = "proposed"
        self.save()
        if self.icfg.get("approve"):
            if not _confirm(v):
                v.status, v.kept = "rejected", False
                self.history.append({"version": f"v{v.index}", "result": "rejected by user", "summary": v.change.get("summary"),
                                     "params": v.change.get("params"), "autopilot_pid": v.change.get("autopilot_pid"),
                                     "diff": v.change.get("diff")})
                self.last_index = v.index
                self.save()
                return True
            v.status = "approved"
            self.save()
        self._finish_version(v)
        return True

    def _validate(self) -> None:
        best = self.best
        done = {val["config"] for val in self.validation}
        for vc in self.icfg.get("validation_configs") or []:
            if str(vc) in done:
                continue  # уже провалідовано до переривання
            vcfg = load_config(vc)
            vcfg["controller_params"] = best.config.get("controller_params", {})
            vcfg["autopilot_pid"] = best.config.get("autopilot_pid", {})
            vcfg.setdefault("record", {})["extra_sources"] = []
            vdir = self.run_dir / "validation" / Path(vc).stem
            vdir.mkdir(parents=True, exist_ok=True)
            vcfg["record"]["sessions_dir"] = str(vdir / "sessions")
            vcfg["analysis"]["enabled"] = False
            cpath = vdir / "config.yaml"
            cpath.write_text(yaml.safe_dump({k: x for k, x in vcfg.items() if not k.startswith("_")},
                                            allow_unicode=True, sort_keys=False), encoding="utf-8")
            _log(f"Валідація найкращої версії v{best.index} на {vc}...")
            time.sleep(float(self.icfg.get("pause_between_runs_s", 3.0)))
            sess = _run_version(best.entry_path, cpath, Path(vcfg["record"]["sessions_dir"]), self.extra_path, vdir / "run.log")
            if sess is None:
                self.validation.append({"config": str(vc), "status": "failed_to_run"})
            else:
                metrics, _ = _analyze(sess, self.analysis_cfg, False, self.budget)
                summary = json.loads((sess / "summary.json").read_text(encoding="utf-8"))
                self.validation.append({"config": str(vc), "status": "run", "session": str(sess),
                                        "evaluation": evaluate(self.criteria, metrics, summary)})
            self.save()

    def _write_report(self) -> Path:
        best, v0 = self.best, self.version(0)
        result = {
            "run_dir": str(self.run_dir), "stop_reason": self.stop_reason, "best_version": f"v{best.index}",
            "baseline": v0.evaluation, "best": best.evaluation, "criteria": self.criteria,
            "criteria_notes": self.criteria_notes, "history": self.history, "validation": self.validation,
            "best_controller_params": best.config.get("controller_params"),
            "best_autopilot_pid": best.config.get("autopilot_pid"),
            "cost_usd_estimate": round(self.budget.spent, 3), "cost_items": self.budget.items,
        }
        (self.run_dir / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2, default=str),
                                                   encoding="utf-8")
        return render_improve_report(self.ws, best, result)

    # ── головний цикл ────────────────────────────────────────────────────
    def run(self) -> Path:
        _log(f"Робоча тека: {self.run_dir}")
        try:
            self._recover()
            v0 = self.version(0)
            if self.best_index is None:
                if v0.status != "scored":
                    raise SystemExit("[uavlab improve] Базовий тест (v0) не відбувся — перевірте Unreal і "
                                     f"`uavlab run` вручну, потім: uavlab improve --resume \"{self.run_dir}\"")
                self.best_index = 0
                self.save()

            if self.phase == "loop":
                while self._iteration():
                    pass
                self.phase = "validation"
                self.save()
            if self.phase == "validation":
                self._validate()
                self.phase = "report"
                self.save()
        except (KeyboardInterrupt, ImproveInterrupted) as exc:
            self.save()
            why = "Ctrl+C" if isinstance(exc, KeyboardInterrupt) else str(exc)
            _log(f"Цикл перервано ({why}). Стан збережено.")
            if self.best is not None and self.best.evaluation:
                _log(f"Проміжний звіт: {self._write_report()}")
            _log(f"Продовжити з останнього завершеного кроку: uavlab improve --resume \"{self.run_dir}\"")
            raise SystemExit(130 if isinstance(exc, KeyboardInterrupt) else 2)

        out = self._write_report()
        self.phase = "done"
        self.save()
        best, v0 = self.best, self.version(0)
        _log("=" * 60)
        _log(f"Завершено: {self.stop_reason}. Найкраща версія: v{best.index} "
             f"({best.evaluation['passed']}/{best.evaluation['total']} критеріїв; база v0: "
             f"{v0.evaluation['passed']}/{v0.evaluation['total']}). Оцінка вартості: ${self.budget.spent:.2f}")
        _log(f"Звіт: {out}")
        if best is not v0:
            _log(f"Перенести в оригінальні файли: uavlab apply \"{self.run_dir}\"")
        return self.run_dir


def run_improve(controller_path: str | Path, cfg: dict) -> Path:
    return ImproveRun.create(Path(controller_path), cfg).run()


def resume_improve(run_dir: str | Path, improve_overrides: Optional[dict] = None) -> Path:
    return ImproveRun.resume(Path(run_dir), improve_overrides or {}).run()


def apply_run(run_dir: str | Path) -> None:
    """Переносить найкращу версію: код — у оригінальні файли (з резервною копією .bak-<час>),
    параметри — у новий файл конфігу поруч з оригіналом (*.improved.yaml), PID — підказка для Blueprint."""
    run_dir = Path(run_dir).resolve()
    summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
    origin = json.loads((run_dir / "origin.json").read_text(encoding="utf-8"))
    best = run_dir / summary["best_version"]
    if summary["best_version"] == "v0":
        print("Найкраща версія — v0 (без змін); переносити нічого.")
        return

    stamp = time.strftime("%Y%m%d_%H%M%S")
    for name, original in origin["files"].items():
        new = best / "src" / name
        if new.exists() and new.read_bytes() != Path(original).read_bytes():
            shutil.copy2(original, f"{original}.bak-{stamp}")
            shutil.copy2(new, original)
            print(f"Оновлено: {original}  (резервна копія: {original}.bak-{stamp})")

    best_cfg = yaml.safe_load((best / "config.yaml").read_text(encoding="utf-8"))
    base_path = origin.get("config")
    base = yaml.safe_load(Path(base_path).read_text(encoding="utf-8")) if base_path else {}
    base["controller_params"] = best_cfg.get("controller_params", {})
    if best_cfg.get("autopilot_pid"):
        base["autopilot_pid"] = best_cfg["autopilot_pid"]
    target = Path(base_path).with_suffix(".improved.yaml") if base_path else run_dir / "best_config.yaml"
    target.write_text(yaml.safe_dump(base, allow_unicode=True, sort_keys=False), encoding="utf-8")
    print(f"Конфіг з найкращими параметрами: {target}")

    if best_cfg.get("autopilot_pid"):
        print("\nPID автопілота діяли лише на час тестів. Щоб зробити їх постійними, внесіть у Blueprint літака "
              "(компонент AttitudeControl):")
        for axis, vals in best_cfg["autopilot_pid"].items():
            pid_name = {"roll": "RollPid", "pitch": "PitchPid", "yaw_rate": "YawRatePid"}[axis]
            print(f"  {pid_name}: " + ", ".join(f"{k}={v}" for k, v in vals.items()))
        print("  (або залиште autopilot_pid у конфігу — стенд застосовуватиме їх на кожному тесті)")
