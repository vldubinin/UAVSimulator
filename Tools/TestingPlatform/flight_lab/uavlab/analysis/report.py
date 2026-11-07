"""report.json -> report.html (самодостатній, графіки поруч у plots/) + короткий підсумок у консоль."""

from __future__ import annotations

import html
import json
from pathlib import Path
from typing import Optional

SEV_COLORS = {"critical": "#b71c1c", "high": "#e65100", "medium": "#f9a825", "low": "#1565c0", "info": "#546e7a"}
SEV_UK = {"critical": "критично", "high": "високий", "medium": "середній", "low": "низький", "info": "інфо"}
VERDICT_UK = {"pass": "ПРОЙДЕНО", "marginal": "НА МЕЖІ", "fail": "НЕ ПРОЙДЕНО", "inconclusive": "НЕВИЗНАЧЕНО"}
VERDICT_COLORS = {"pass": "#2e7d32", "marginal": "#f9a825", "fail": "#c62828", "inconclusive": "#546e7a"}
ATTR_UK = {"user_algorithm": "алгоритм користувача", "inner_autopilot_pid": "внутрішній PID автопілота",
           "test_conditions": "умови тесту", "simulator": "симулятор", "unclear": "неясно"}

CSS = """
:root{--bg:#fafafa;--fg:#212121;--card:#fff;--muted:#616161;--border:#e0e0e0}
@media (prefers-color-scheme:dark){:root{--bg:#121212;--fg:#e0e0e0;--card:#1e1e1e;--muted:#9e9e9e;--border:#333}}
body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,Segoe UI,Roboto,sans-serif;margin:0;padding:16px}
main{max-width:1100px;margin:auto}
h1{margin:.2em 0}h2{margin-top:1.6em;border-bottom:1px solid var(--border);padding-bottom:.2em}
.card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:12px 16px;margin:10px 0}
.badge{display:inline-block;color:#fff;border-radius:4px;padding:2px 8px;font-size:12px;font-weight:600;margin-right:6px}
.verdict{font-size:20px;font-weight:700;padding:6px 14px;border-radius:6px;color:#fff;display:inline-block}
.muted{color:var(--muted);font-size:13px}
pre{background:rgba(127,127,127,.12);padding:10px;border-radius:6px;overflow-x:auto;font-size:13px}
table{border-collapse:collapse;width:100%;font-size:13px}td,th{border:1px solid var(--border);padding:4px 8px;text-align:left}
img{max-width:100%;border:1px solid var(--border);border-radius:6px;margin:6px 0;background:#fff}
ul{margin:.3em 0 .3em 1.2em;padding:0}
"""


def _e(x) -> str:
    return html.escape(str(x)) if x is not None else ""


def _kv_table(d: dict) -> str:
    rows = "".join(f"<tr><th>{_e(k)}</th><td>{_e(v)}</td></tr>" for k, v in d.items())
    return f"<table>{rows}</table>"


def render_html(report: Optional[dict], metrics: dict, summary: dict, plots: list[Path], out: Path,
                error: Optional[str] = None) -> Path:
    parts = [f"<!doctype html><html lang='uk'><head><meta charset='utf-8'>"
             f"<meta name='viewport' content='width=device-width,initial-scale=1'>"
             f"<title>Звіт льотного випробування</title><style>{CSS}</style></head><body><main>"]
    parts.append(f"<h1>{_e(summary.get('test_name'))}</h1>")
    parts.append(f"<div class='muted'>Сесія {_e(summary.get('session_id'))} · алгоритм "
                 f"{_e(summary.get('controller_file'))} ({_e(summary.get('controller_class'))}) · "
                 f"зупинка: {_e(summary.get('stop_reason'))} · {float(summary.get('sim_duration_s') or 0):.1f} с</div>")
    if summary.get("objective"):
        parts.append(f"<div class='card'><b>Мета тесту:</b> {_e(summary['objective'])}</div>")

    if report:
        v = report.get("verdict", "inconclusive")
        parts.append(f"<h2>Висновок</h2><span class='verdict' style='background:{VERDICT_COLORS.get(v, '#546e7a')}'>"
                     f"{VERDICT_UK.get(v, v)}</span> <span class='muted'>впевненість: {_e(report.get('confidence'))}</span>")
        parts.append(f"<div class='card'>{_e(report.get('summary'))}</div>")
        parts.append(f"<div class='card'><b>Досягнення мети:</b> {_e(report.get('objective_assessment'))}</div>")
        if report.get("strengths"):
            parts.append("<h2>Сильні сторони</h2><ul>" + "".join(f"<li>{_e(s)}</li>" for s in report["strengths"]) + "</ul>")
        issues = sorted(report.get("issues", []), key=lambda i: list(SEV_COLORS).index(i.get("severity", "info")))
        parts.append(f"<h2>Проблеми та слабкі місця ({len(issues)})</h2>")
        for i in issues:
            sev = i.get("severity", "info")
            parts.append("<div class='card'>")
            parts.append(f"<span class='badge' style='background:{SEV_COLORS.get(sev)}'>{SEV_UK.get(sev, sev)}</span>"
                         f"<span class='badge' style='background:#455a64'>{_e(i.get('category'))}</span>"
                         f"<b>{_e(i.get('id'))}: {_e(i.get('title'))}</b>")
            parts.append(f"<div class='muted'>Причина в: {ATTR_UK.get(i.get('attribution'), _e(i.get('attribution')))}</div>")
            if i.get("evidence"):
                parts.append("<b>Докази:</b><ul>" + "".join(f"<li>{_e(x)}</li>" for x in i["evidence"]) + "</ul>")
            parts.append(f"<b>Гіпотеза:</b> {_e(i.get('root_cause_hypothesis'))}<br>")
            parts.append(f"<b>Рекомендація:</b> {_e(i.get('recommendation'))}")
            if i.get("code_suggestion"):
                parts.append(f"<pre>{_e(i['code_suggestion'])}</pre>")
            parts.append("</div>")
        if report.get("recommended_next_tests"):
            parts.append("<h2>Наступні тести</h2><ul>" +
                         "".join(f"<li>{_e(s)}</li>" for s in report["recommended_next_tests"]) + "</ul>")
        if report.get("caveats"):
            parts.append("<h2>Обмеження аналізу</h2><ul>" + "".join(f"<li>{_e(s)}</li>" for s in report["caveats"]) + "</ul>")
        meta = report.get("_meta")
        if meta:
            parts.append(f"<div class='muted'>LLM: {_e(meta.get('model'))} · вхід {_e(meta.get('input_tokens'))} / "
                         f"вихід {_e(meta.get('output_tokens'))} токенів</div>")
    else:
        parts.append(f"<h2>LLM-аналіз недоступний</h2><div class='card'>{_e(error or 'аналіз вимкнено')}</div>")

    if plots:
        parts.append("<h2>Графіки</h2>")
        for p in plots:
            parts.append(f"<img src='plots/{_e(p.name)}' alt='{_e(p.stem)}'>")

    parts.append("<h2>Ключові метрики</h2>")
    run = metrics.get("run", {})
    parts.append(_kv_table(run))
    for key in ("attitude_tracking", "oscillation", "saturation", "controller_timing", "link", "environment", "flight"):
        if metrics.get(key):
            parts.append(f"<details><summary><b>{_e(key)}</b></summary>"
                         f"<pre>{_e(json.dumps(metrics[key], ensure_ascii=False, indent=1))}</pre></details>")
    if metrics.get("anomalies"):
        rows = "".join(f"<tr><td>{_e(a.get('t_start'))}–{_e(a.get('t_end'))}</td><td>{_e(a.get('kind'))}</td>"
                       f"<td>{_e({k: v for k, v in a.items() if k not in ('t_start', 't_end', 'kind')})}</td></tr>"
                       for a in metrics["anomalies"])
        parts.append(f"<h2>Аномалії ({metrics.get('anomalies_total', len(metrics['anomalies']))})</h2>"
                     f"<table><tr><th>t, с</th><th>тип</th><th>деталі</th></tr>{rows}</table>")
    parts.append("</main></body></html>")
    out.write_text("".join(parts), encoding="utf-8")
    return out


def print_summary(report: Optional[dict]) -> None:
    if not report:
        return
    v = report.get("verdict")
    print(f"\n=== Вердикт: {VERDICT_UK.get(v, v)} (впевненість {report.get('confidence')}) ===")
    print(report.get("summary", ""))
    for i in report.get("issues", []):
        print(f"  [{SEV_UK.get(i.get('severity'), i.get('severity'))}] {i.get('id')}: {i.get('title')}")
        print(f"      -> {i.get('recommendation')}")
