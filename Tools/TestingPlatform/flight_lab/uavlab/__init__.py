"""
uavlab — лабораторний стенд віртуальних льотних випробувань для UAVSimulator.

Мінімальний алгоритм керування:

    from uavlab import Controller, Command

    class MyAutopilot(Controller):
        def step(self, obs):
            return Command(roll_deg=0, pitch_deg=2, yaw_rate_deg_s=0, thrust=0.7)

Запуск:  uavlab run my_autopilot.py --config test.yaml
Аналіз:  uavlab analyze sessions/<session_id>

Докладно — Docs/14-Flight-Test-Lab.md.
"""

from .controller import Command, Context, Controller
from .observation import Observation, Truth

__all__ = ["Command", "Context", "Controller", "Observation", "Truth", "run"]
__version__ = "0.1.0"


def run(controller_path, config_path=None, **overrides):
    """Програмний запуск тесту (те саме, що `uavlab run`). Повертає шлях до теки сесії."""
    from .bench import run_test
    from .config import load_config

    return run_test(controller_path, load_config(config_path, overrides))
