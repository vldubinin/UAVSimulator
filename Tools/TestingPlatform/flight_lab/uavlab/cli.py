"""
uavlab run <algo.py> [--config test.yaml] [--start auto|wait] [--mode AutoTrack] [--time-limit S] [--no-analysis]
uavlab analyze <sessions/<id>> [--no-llm] [--model claude-opus-5-5]
"""

from __future__ import annotations

import argparse
import sys


def main(argv=None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # кирилиця в консолі Windows

    p = argparse.ArgumentParser(prog="uavlab", description="Стенд віртуальних льотних випробувань UAVSimulator")
    sub = p.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run", help="провести тест алгоритму")
    r.add_argument("controller", help="файл алгоритму (.py) з підкласом uavlab.Controller або функцією step(obs)")
    r.add_argument("--config", "-c", help="YAML-конфіг тесту")
    r.add_argument("--start", choices=["auto", "wait"], help="auto — стенд стартує симуляцію; wait — Start у меню Unreal")
    r.add_argument("--mode", help="ESimulatorMode для start: auto (AutoTrack, PlaybackAndAutoTrack)")
    r.add_argument("--time-limit", type=float, help="ліміт тривалості, с (симуляційний час)")
    r.add_argument("--no-analysis", action="store_true", help="не запускати аналіз після тесту")
    r.add_argument("--no-llm", action="store_true", help="аналіз лише метриками, без Claude")

    a = sub.add_parser("analyze", help="(пере)аналізувати записану сесію без Unreal")
    a.add_argument("session_dir")
    a.add_argument("--config", "-c", help="YAML-конфіг (береться секція analysis)")
    a.add_argument("--no-llm", action="store_true")
    a.add_argument("--model")
    a.add_argument("--language")

    args = p.parse_args(argv)

    from .config import load_config

    if args.cmd == "run":
        from .bench import run_test

        overrides: dict = {}
        if args.start:
            overrides["start"] = args.start
        if args.mode:
            overrides["mode"] = args.mode
        if args.time_limit is not None:
            overrides.setdefault("stop", {})["time_limit_s"] = args.time_limit
        if args.no_analysis:
            overrides.setdefault("analysis", {})["enabled"] = False
        cfg = load_config(args.config, overrides)
        if args.no_llm and cfg["analysis"].get("enabled", True):
            cfg["analysis"]["enabled"] = False
            session_dir = run_test(args.controller, cfg)
            from .analysis import analyze_session
            analyze_session(session_dir, cfg["analysis"], use_llm=False)
        else:
            run_test(args.controller, cfg)
        return 0

    if args.cmd == "analyze":
        from .analysis import analyze_session

        cfg = load_config(args.config)["analysis"]
        if args.model:
            cfg["model"] = args.model
        if args.language:
            cfg["language"] = args.language
        analyze_session(args.session_dir, cfg, use_llm=not args.no_llm)
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
