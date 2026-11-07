"""Пост-аналіз записаної сесії: метрики -> графіки -> пакет -> Claude -> report.json/report.html."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

from ..recorder import load_session
from .metrics import compute_metrics
from .pack import build_pack, pack_to_text
from .plots import make_plots
from .report import print_summary, render_html


def analyze_session(session_dir: str | Path, cfg: Optional[dict] = None, use_llm: bool = True) -> Path:
    from ..config import DEFAULTS

    cfg = {**DEFAULTS["analysis"], **(cfg or {})}
    session = load_session(session_dir)
    d = session["dir"]
    print(f"[uavlab] Аналіз сесії {d.name}: метрики...", flush=True)

    metrics = compute_metrics(session)
    (d / "metrics.json").write_text(json.dumps(metrics, ensure_ascii=False, indent=2, default=str), encoding="utf-8")

    plots = make_plots(session, metrics, d / "plots")
    pack = build_pack(session, metrics, include_source=cfg.get("include_source", True))
    blocks = pack_to_text(pack)
    (d / "llm_input.md").write_text("\n\n".join(f"## {n}\n{t}" for n, t in blocks), encoding="utf-8")

    report, error = None, None
    if use_llm:
        from .llm_claude import analyze_with_claude
        print(f"[uavlab] Надсилаю пакет у Claude ({cfg.get('model')})...", flush=True)
        try:
            report = analyze_with_claude(blocks, plots if cfg.get("include_plots", True) else [], cfg)
            (d / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        except Exception as exc:  # звіт з метриками все одно генеруємо
            error = str(exc)
            print(f"[uavlab] LLM-аналіз не вдався: {error}", flush=True)
    else:
        error = "LLM-аналіз вимкнено (--no-llm)"

    out = render_html(report, metrics, session["summary"], plots, d / "report.html", error=error)
    print_summary(report)
    print(f"[uavlab] Звіт: {out.resolve()}", flush=True)
    return out
