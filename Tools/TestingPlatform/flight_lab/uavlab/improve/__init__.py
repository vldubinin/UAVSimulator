"""
uavlab improve — замкнений цикл «тест → аналіз → правка LLM → повторний тест» (варіант A, Claude API).

    uavlab improve my_algo.py -c test.yaml [--max-iterations 5] [--max-cost 5] [--approve] [--allow params,code,autopilot_pid]
    uavlab apply improve/<run_id>          # перенести найкращу версію в оригінальні файли

Алгоритм:
  v0 = копія оригіналу -> тест -> аналіз -> оцінка за критеріями приймання (якщо їх немає в конфігу —
  Claude один раз перетворює objective на формальні критерії, вони заморожуються).
  Далі, поки не пройдено / не вичерпано ітерації або бюджет:
    нова версія = копія НАЙКРАЩОЇ -> агент вносить правки -> (--approve: підтвердження) -> тест -> аналіз
    -> оцінка; краща — стає найкращою, інакше відкидається (агент бачить це в історії).
  Наприкінці — валідація найкращої версії на improve.validation_configs і звіт improve_report.html.
Кожен тест — окремий процес `uavlab run` (чисті імпорти модулів алгоритму, ізоляція падінь).
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
    if last is not None and last is not best and last.status == "run":
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


def run_improve(controller_path: str | Path, cfg: dict) -> Path:
    controller_path = Path(controller_path).resolve()
    icfg = cfg["improve"]
    extra_path = str(controller_path.parent)  # нескопійовані локальні залежності алгоритму
    run_dir = Path(icfg.get("runs_dir", "improve")).resolve() / f"{time.strftime('%Y%m%d_%H%M%S')}_{controller_path.stem}"
    ws = Workspace.create(run_dir, controller_path, cfg)
    budget = Budget(float(icfg.get("max_cost_usd", 5.0)))
    analysis_cfg = dict(cfg["analysis"])
    use_llm_analysis = bool(icfg.get("analyze_each_iteration", True))
    history: list[dict] = []
    criteria: list[dict] = list(cfg.get("acceptance") or [])
    criteria_notes = ""
    stop_reason = "max_iterations"
    _log(f"Робоча тека: {run_dir}")

    def run_and_score(v: Version) -> bool:
        nonlocal criteria, criteria_notes
        _log(f"Тест версії v{v.index}...")
        session = _run_version(v.entry_path, v.config_path, Path(v.config["record"]["sessions_dir"]), extra_path,
                               v.dir / "run.log")
        if session is None:
            v.status = "failed_to_run"
            _log(f"v{v.index}: тест не відбувся (див. {v.dir / 'run.log'})")
            return False
        v.session_dir, v.status = session, "run"
        metrics, v.report = _analyze(session, analysis_cfg, use_llm_analysis, budget)
        if not criteria:
            _log("Критеріїв приймання немає — генерую з objective (один раз, далі вони заморожені)...")
            criteria, criteria_notes, usage = generate_criteria(cfg.get("objective", ""), metrics, icfg)
            budget.add("acceptance criteria", usage_cost(usage))
            if not criteria:
                raise SystemExit("[uavlab improve] Не вдалося сформулювати критерії — задайте acceptance у конфігу.")
            for c in criteria:
                op = "max" if "max" in c else "min"
                _log(f"  критерій: {c['metric']} {'<=' if op == 'max' else '>='} {c[op]}  ({c.get('rationale', '')})")
            if criteria_notes:
                _log(f"  примітка: {criteria_notes}")
            if icfg.get("approve") and input("Прийняти ці критерії? [y/N] ").strip().lower() not in ("y", "yes", "т", "так"):
                raise SystemExit("[uavlab improve] Критерії не прийнято — задайте acceptance у конфігу вручну.")
        (run_dir / "acceptance.yaml").write_text(yaml.safe_dump(criteria, allow_unicode=True, sort_keys=False), encoding="utf-8")
        v.evaluation = _evaluate_version(v, criteria, metrics)
        v._metrics = metrics  # type: ignore[attr-defined]
        e = v.evaluation
        _log(f"v{v.index}: пройдено {e['passed']}/{e['total']} критеріїв, перевищення {e['violation']}"
             + (f", ПРОВАЛ: {e['hard_fail']}" if e["hard_fail"] else ""))
        return True

    # ── v0: базова лінія ──────────────────────────────────────────────────
    v0 = ws.versions[0]
    if not run_and_score(v0):
        raise SystemExit("[uavlab improve] Базовий тест (v0) не відбувся — перевірте Unreal і `uavlab run` вручну.")
    v0.kept = True
    best: Version = v0
    last: Optional[Version] = None
    agent = ChangeAgent(icfg, language=analysis_cfg.get("language", "uk"))

    for it in range(1, int(icfg.get("max_iterations", 5)) + 1):
        if best.evaluation["all_passed"]:
            stop_reason = "all_criteria_passed"
            break
        if budget.spent + agent.cost >= budget.max_usd:
            stop_reason = "budget_exhausted"
            break

        v = ws.branch(best)
        _log(f"Ітерація {it}: агент готує правки (база v{best.index})...")
        try:
            v.change = agent.propose(v, _context(ws, best, last, criteria, cfg, history, best._metrics), extra_path)  # type: ignore[attr-defined]
        except Exception as exc:
            _log(f"Агент не зміг запропонувати правки: {exc}")
            stop_reason = "agent_error"
            shutil.rmtree(v.dir, ignore_errors=True)
            ws.versions.pop()
            break
        v.save_change()
        no_changes = not (v.change.get("diff") or v.change.get("params") or v.change.get("autopilot_pid"))
        _log(f"  {v.change.get('summary', '')}")
        if no_changes:
            stop_reason = "agent_stopped" if v.change.get("stop_improving") else "no_changes"
            v.status = "rejected"
            history.append({"version": f"v{v.index}", "result": "no changes proposed", "summary": v.change.get("summary")})
            break
        if icfg.get("approve") and not _confirm(v):
            v.status, v.kept = "rejected", False
            history.append({"version": f"v{v.index}", "result": "rejected by user", "summary": v.change.get("summary"),
                            "params": v.change.get("params"), "autopilot_pid": v.change.get("autopilot_pid"),
                            "diff": v.change.get("diff")})
            last = v
            continue

        time.sleep(float(icfg.get("pause_between_runs_s", 3.0)))
        ok = run_and_score(v)
        v.kept = bool(ok and is_better(v.evaluation, best.evaluation))
        history.append({
            "version": f"v{v.index}", "base": f"v{best.index}",
            "result": "kept (new best)" if v.kept else ("failed to run" if not ok else "reverted (not better)"),
            "summary": v.change.get("summary"), "hypothesis": v.change.get("hypothesis"),
            "params": v.change.get("params"), "autopilot_pid": v.change.get("autopilot_pid"),
            "code_edits": v.change.get("code_edits"),
            "evaluation": {k: (v.evaluation or {}).get(k) for k in ("passed", "total", "violation", "hard_fail")},
            "criteria_values": [(d["metric"], d["value"], d["ok"]) for d in (v.evaluation or {}).get("details", [])],
        })
        _log(f"  -> {history[-1]['result']}")
        last = v
        if v.kept:
            best = v
    else:
        if best.evaluation["all_passed"]:
            stop_reason = "all_criteria_passed"

    budget.add("change agent", agent.cost)

    # ── валідація на додаткових сценаріях ────────────────────────────────
    validation = []
    for vc in icfg.get("validation_configs") or []:
        vcfg = load_config(vc)
        vcfg["controller_params"] = best.config.get("controller_params", {})
        vcfg["autopilot_pid"] = best.config.get("autopilot_pid", {})
        vcfg.setdefault("record", {})["extra_sources"] = []
        vdir = run_dir / "validation" / Path(vc).stem
        vdir.mkdir(parents=True, exist_ok=True)
        vcfg["record"]["sessions_dir"] = str(vdir / "sessions")
        vcfg["analysis"]["enabled"] = False
        cpath = vdir / "config.yaml"
        cpath.write_text(yaml.safe_dump({k: x for k, x in vcfg.items() if not k.startswith("_")},
                                        allow_unicode=True, sort_keys=False), encoding="utf-8")
        _log(f"Валідація найкращої версії v{best.index} на {vc}...")
        time.sleep(float(icfg.get("pause_between_runs_s", 3.0)))
        sess = _run_version(best.entry_path, cpath, Path(vcfg["record"]["sessions_dir"]), extra_path, vdir / "run.log")
        if sess is None:
            validation.append({"config": str(vc), "status": "failed_to_run"})
            continue
        metrics, _ = _analyze(sess, analysis_cfg, False, budget)
        summary = json.loads((sess / "summary.json").read_text(encoding="utf-8"))
        validation.append({"config": str(vc), "status": "run", "session": str(sess),
                           "evaluation": evaluate(criteria, metrics, summary)})

    result = {
        "run_dir": str(run_dir), "stop_reason": stop_reason, "best_version": f"v{best.index}",
        "baseline": v0.evaluation, "best": best.evaluation, "criteria": criteria, "criteria_notes": criteria_notes,
        "history": history, "validation": validation,
        "best_controller_params": best.config.get("controller_params"),
        "best_autopilot_pid": best.config.get("autopilot_pid"),
        "cost_usd_estimate": round(budget.spent, 3), "cost_items": budget.items,
    }
    (run_dir / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
    out = render_improve_report(ws, best, result)

    _log("=" * 60)
    _log(f"Завершено: {stop_reason}. Найкраща версія: v{best.index} "
         f"({best.evaluation['passed']}/{best.evaluation['total']} критеріїв; база v0: "
         f"{v0.evaluation['passed']}/{v0.evaluation['total']}). Оцінка вартості: ${budget.spent:.2f}")
    _log(f"Звіт: {out}")
    if best is not v0:
        _log(f"Перенести в оригінальні файли: uavlab apply \"{run_dir}\"")
    return run_dir


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
