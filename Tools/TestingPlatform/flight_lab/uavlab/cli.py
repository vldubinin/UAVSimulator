"""
uavlab run <algo.py> [--config test.yaml] [--start auto|wait] [--mode AutoTrack] [--time-limit S] [--no-analysis]
uavlab analyze <sessions/<id>> [--no-llm] [--model claude-opus-5-5]
uavlab improve <algo.py> -c test.yaml [--max-iterations N] [--max-cost USD] [--allow params,code,autopilot_pid] [--approve]
uavlab improve --resume <improve/<run_id>> [--max-iterations N] [--max-cost USD] [--approve]
uavlab apply <improve/<run_id>>
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
    r.add_argument("--map", action="store_true", help="жива карта: літак, трек, точки маршруту (tkintermapview)")

    a = sub.add_parser("analyze", help="(пере)аналізувати записану сесію без Unreal")
    a.add_argument("session_dir")
    a.add_argument("--config", "-c", help="YAML-конфіг (береться секція analysis)")
    a.add_argument("--no-llm", action="store_true")
    a.add_argument("--model")
    a.add_argument("--language")

    im = sub.add_parser("improve", help="цикл тест → аналіз → правка Claude → повторний тест")
    im.add_argument("controller", nargs="?", help="файл алгоритму (не потрібен з --resume)")
    im.add_argument("--resume", metavar="RUN_DIR",
                    help="продовжити перерваний прогін з останнього завершеного кроку (тека improve/<run_id>)")
    im.add_argument("--config", "-c")
    im.add_argument("--max-iterations", type=int)
    im.add_argument("--max-cost", type=float, help="бюджет, $ (оцінка за токенами)")
    im.add_argument("--allow", help="що можна змінювати: params,code,autopilot_pid")
    im.add_argument("--approve", action="store_true", help="підтверджувати кожну правку перед тестом")
    im.add_argument("--no-iteration-analysis", action="store_true",
                    help="не робити повний LLM-аналіз кожної версії (дешевше; агент бачить лише метрики)")

    ap = sub.add_parser("apply", help="перенести найкращу версію improve в оригінальні файли")
    ap.add_argument("run_dir")

    args = p.parse_args(argv)

    if args.cmd == "improve":
        from .config import load_config
        from .improve import resume_improve, run_improve

        ic: dict = {}
        if args.max_iterations is not None:
            ic["max_iterations"] = args.max_iterations
        if args.max_cost is not None:
            ic["max_cost_usd"] = args.max_cost
        if args.allow:
            ic["allow"] = [a.strip() for a in args.allow.split(",") if a.strip()]
        if args.approve:
            ic["approve"] = True
        if args.no_iteration_analysis:
            ic["analyze_each_iteration"] = False

        if args.resume:
            if args.controller or args.config:
                p.error("з --resume алгоритм і конфіг беруться з теки прогону — не вказуйте їх")
            resume_improve(args.resume, ic)  # прапорці CLI перекривають збережені налаштування improve
            return 0
        if not args.controller:
            p.error("вкажіть файл алгоритму або --resume <тека прогону>")
        cfg = load_config(args.config)
        if cfg.get("start") != "auto":
            cfg["start"] = "auto"  # цикл перезапускає тести сам
        cfg["improve"].update(ic)
        run_improve(args.controller, cfg)
        return 0

    if args.cmd == "apply":
        from .improve import apply_run

        apply_run(args.run_dir)
        return 0

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
        if args.map:
            overrides.setdefault("viz", {})["enabled"] = True
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
