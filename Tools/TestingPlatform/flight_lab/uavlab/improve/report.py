"""improve_report.html: критерії (база vs найкраща), історія ітерацій з правками й diff, валідація, вартість."""

from __future__ import annotations

import html
import json
from pathlib import Path

from ..analysis.report import CSS


def _e(x) -> str:
    return html.escape(str(x)) if x is not None else "—"


def _fmt(v) -> str:
    return "—" if v is None else (f"{v:.3g}" if isinstance(v, float) else str(v))


def render_improve_report(ws, best, result: dict) -> Path:
    base_ev, best_ev = result["baseline"] or {}, result["best"] or {}
    p = ["<!doctype html><html lang='uk'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         f"<title>Автоматичне покращення</title><style>{CSS}"
         ".ok{color:#2e7d32;font-weight:600}.bad{color:#c62828;font-weight:600}</style></head><body><main>"]
    p.append(f"<h1>Автоматичне покращення алгоритму</h1><div class='muted'>{_e(result['run_dir'])}</div>")
    p.append(f"<div class='card'>Причина завершення: <b>{_e(result['stop_reason'])}</b> · найкраща версія: "
             f"<b>{_e(result['best_version'])}</b> · критеріїв пройдено: v0 {base_ev.get('passed')}/{base_ev.get('total')} → "
             f"{result['best_version']} {best_ev.get('passed')}/{best_ev.get('total')} · оцінка вартості "
             f"${result['cost_usd_estimate']:.2f}</div>")

    # Критерії
    rows = []
    bd = {d["metric"]: d for d in base_ev.get("details", [])}
    for d in best_ev.get("details", []):
        b = bd.get(d["metric"], {})
        sign = "≤" if d["op"] == "max" else "≥"
        rows.append(f"<tr><td>{_e(d['metric'])}</td><td>{sign} {_fmt(d['threshold'])}</td>"
                    f"<td class='{'ok' if b.get('ok') else 'bad'}'>{_fmt(b.get('value'))}</td>"
                    f"<td class='{'ok' if d['ok'] else 'bad'}'>{_fmt(d['value'])}</td></tr>")
    p.append("<h2>Критерії приймання (заморожені)</h2><table><tr><th>метрика</th><th>поріг</th><th>v0</th>"
             f"<th>{_e(result['best_version'])}</th></tr>{''.join(rows)}</table>")
    if result.get("criteria_notes"):
        p.append(f"<div class='muted'>Примітка до критеріїв: {_e(result['criteria_notes'])}</div>")

    # Найкращі параметри
    if result.get("best_controller_params") or result.get("best_autopilot_pid"):
        p.append("<h2>Підсумкові параметри</h2><pre>" + _e(json.dumps(
            {"controller_params": result.get("best_controller_params"), "autopilot_pid": result.get("best_autopilot_pid")},
            ensure_ascii=False, indent=2)) + "</pre>")

    # Історія
    p.append("<h2>Ітерації</h2>")
    for v in ws.versions[1:]:
        ch = v.change or {}
        ev = v.evaluation or {}
        verdict = ("нова найкраща" if v.kept else {"rejected": "відхилено / без змін", "failed_to_run": "тест не відбувся"}
                   .get(v.status, "відкинуто (не краще)"))
        p.append("<div class='card'>")
        p.append(f"<b>v{v.index}</b> (від v{v.parent}) — <span class='{'ok' if v.kept else 'bad'}'>{verdict}</span>"
                 + (f" · {ev.get('passed')}/{ev.get('total')} критеріїв, перевищення {ev.get('violation')}" if ev else "")
                 + (f" · <span class='bad'>провал: {_e(ev.get('hard_fail'))}</span>" if ev.get("hard_fail") else ""))
        p.append(f"<div>{_e(ch.get('summary'))}</div><div class='muted'>Гіпотеза: {_e(ch.get('hypothesis'))}</div>")
        if ch.get("params"):
            p.append(f"<div>controller_params: <code>{_e(json.dumps(ch['params'], ensure_ascii=False))}</code></div>")
        if ch.get("autopilot_pid"):
            p.append(f"<div>autopilot_pid: <code>{_e(json.dumps(ch['autopilot_pid'], ensure_ascii=False))}</code></div>")
        if ch.get("diff"):
            p.append(f"<details><summary>diff коду</summary><pre>{_e(ch['diff'])}</pre></details>")
        if v.session_dir and (v.session_dir / "report.html").exists():
            p.append(f"<div class='muted'><a href='{_e(v.session_dir.as_uri())}/report.html'>звіт аналізу цієї версії</a></div>")
        p.append("</div>")

    if result.get("validation"):
        p.append("<h2>Валідація найкращої версії на інших сценаріях</h2><table><tr><th>сценарій</th><th>результат</th></tr>")
        for val in result["validation"]:
            ev = val.get("evaluation") or {}
            res = (f"{ev.get('passed')}/{ev.get('total')}" + (f", провал: {ev.get('hard_fail')}" if ev.get("hard_fail") else "")
                   if ev else val.get("status"))
            p.append(f"<tr><td>{_e(val['config'])}</td><td class='{'ok' if ev.get('all_passed') else 'bad'}'>{_e(res)}</td></tr>")
        p.append("</table>")

    p.append("<h2>Вартість (оцінка за токенами)</h2><table>"
             + "".join(f"<tr><td>{_e(w)}</td><td>${c:.3f}</td></tr>" for w, c in result["cost_items"]) + "</table>")
    p.append(f"<div class='card'>Оригінальні файли не змінено. Перенести найкращу версію: "
             f"<code>uavlab apply \"{_e(result['run_dir'])}\"</code></div></main></body></html>")
    out = Path(result["run_dir"]) / "improve_report.html"
    out.write_text("".join(p), encoding="utf-8")
    return out
