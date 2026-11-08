"""
LLM-аналіз сесії через Claude (Anthropic SDK).

  * модель — claude-opus-5-5 (конфіг analysis.model), adaptive thinking, effort з конфігу;
  * відповідь — structured output (JSON schema REPORT_SCHEMA), тож звіт завжди парситься;
  * графіки йдуть як зображення (мультимодальний аналіз), решта — текстові блоки в тегах;
  * server-side fallbacks: "default" — якщо класифікатор безпеки відхилить запит, API сам
    повторить його на рекомендованій резервній моделі.

Ключ: ANTHROPIC_API_KEY (або профіль `ant auth login`).
"""

from __future__ import annotations

import base64
import json
from pathlib import Path
from typing import Any

SEVERITIES = ["critical", "high", "medium", "low", "info"]
CATEGORIES = ["stability", "tracking", "saturation", "robustness", "timing", "sensor_handling",
              "safety", "mission", "code_quality"]

REPORT_SCHEMA: dict[str, Any] = {
    "type": "object",
    "additionalProperties": False,
    "required": ["summary", "verdict", "objective_assessment", "strengths", "issues",
                 "recommended_next_tests", "confidence", "caveats"],
    "properties": {
        "summary": {"type": "string", "description": "3-6 речень: що тестувалось і головні висновки"},
        "verdict": {"type": "string", "enum": ["pass", "marginal", "fail", "inconclusive"]},
        "objective_assessment": {"type": "string", "description": "чи досягнуто мету тесту, з цифрами"},
        "strengths": {"type": "array", "items": {"type": "string"}},
        "issues": {
            "type": "array",
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["id", "severity", "category", "title", "evidence", "root_cause_hypothesis",
                             "attribution", "recommendation", "code_suggestion"],
                "properties": {
                    "id": {"type": "string", "description": "короткий ідентифікатор, напр. ISS-1"},
                    "severity": {"type": "string", "enum": SEVERITIES},
                    "category": {"type": "string", "enum": CATEGORIES},
                    "title": {"type": "string"},
                    "evidence": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "конкретні метрики / часові інтервали / події, на які спирається висновок",
                    },
                    "root_cause_hypothesis": {"type": "string"},
                    "attribution": {
                        "type": "string",
                        "enum": ["user_algorithm", "inner_autopilot_pid", "test_conditions", "simulator", "unclear"],
                        "description": "де, найімовірніше, корінь проблеми",
                    },
                    "recommendation": {"type": "string"},
                    "code_suggestion": {
                        "type": "string",
                        "description": "конкретна правка коду алгоритму (фрагмент) або порожній рядок",
                    },
                },
            },
        },
        "recommended_next_tests": {"type": "array", "items": {"type": "string"}},
        "confidence": {"type": "string", "enum": ["high", "medium", "low"]},
        "caveats": {"type": "array", "items": {"type": "string"},
                    "description": "обмеження аналізу: брак даних, неоднозначності"},
    },
}

SYSTEM_PROMPT = """You are a senior flight-test and flight-control engineer reviewing a virtual flight test \
of a fixed-wing UAV control algorithm run in a physics simulator (Unreal Engine, custom aerodynamics).

How the system under test is wired:
- The user's algorithm (Python) runs on a test bench at a fixed rate. Each step it receives an observation \
(sensor topics and, if use_truth is true, ground truth) and returns an attitude setpoint: roll_deg, pitch_deg \
(absolute angles), yaw_rate_deg_s, thrust [0,1].
- The setpoint goes over ZeroMQ to the simulator's inner autopilot (UAttitudeControlComponent): PID loops on \
roll angle, pitch angle and yaw rate drive aileron/elevator/rudder in [-1,1]; thrust is passed through as the \
absolute throttle command and the engine spools up with a lag. The inner PID gains are in the manifest.
- Telemetry "flight_truth" is the simulator's ground truth (attitude, geo position, airspeed, AoA, load factor, \
the inner loop's current target and PID output, age of the last received command, wind and EW-jamming \
intensity at the aircraft). Time t in tables is seconds since the start of recording, in SIMULATION time.
- metrics.time_sync tells whether simulated physics kept up with simulation time (physics_time_ratio ~1). If it is \
invalid, durations / response times / rates are stretched by ~1/ratio: say so and do not blame the algorithm for \
timing that the simulator caused. The bench can substitute simulation time for time.monotonic()/time.time() inside \
the algorithm's modules (sim_clock); code using `from time import monotonic` is not covered.

Your job: find weaknesses, failure modes and risks of the USER'S ALGORITHM and recommend concrete improvements.
- Base every finding on evidence from the provided metrics, time windows, events, plots or source code; cite \
numbers and time ranges. Do not invent data that is not in the inputs.
- Separate root causes: the user's algorithm (outer loop logic, gains, filtering, state machine, timing, sensor \
handling) vs. the inner autopilot PID vs. test conditions (wind, EW zones, sensor configuration) vs. simulator \
artefacts. Mark the attribution accordingly.
- Read the algorithm's source code: point out logic bugs, unhandled sensor dropouts, missing saturation or \
anti-windup, discontinuities, unit mistakes (degrees vs radians), heading wrap-around, hard-coded constants that \
fail under the tested conditions.
- Typical things to check: oscillations / limit cycles (dominant frequency, power share), overshoot and \
settling, steady-state bias, command chattering and rate, actuator / thrust saturation and wind-up, stall risk \
(high AoA, low airspeed), excessive bank or load factor, loss of altitude in turns, stale or dropped commands, \
loop timing jitter, behaviour inside EW or wind zones, mission objective achievement.
- If the data cannot support a conclusion, say so in caveats instead of guessing. Prefer a few well-supported \
issues over many speculative ones, ordered by severity.
- Recommendations must be actionable: what to change, roughly by how much, and how to verify it in the next test.
"""


def _image_block(path: Path) -> dict:
    return {"type": "image", "source": {"type": "base64", "media_type": "image/png",
                                         "data": base64.standard_b64encode(path.read_bytes()).decode("ascii")}}


def build_messages(text_blocks: list[tuple[str, str]], plots: list[Path], language: str) -> list[dict]:
    content: list[dict] = []
    for name, text in text_blocks:
        content.append({"type": "text", "text": f"<{name}>\n{text}\n</{name}>"})
    for p in plots:
        content.append({"type": "text", "text": f"Plot: {p.name}"})
        content.append(_image_block(p))
    lang = {"uk": "Ukrainian", "en": "English"}.get(language, language)
    content.append({"type": "text", "text": (
        "Analyse this flight test and return the report as JSON matching the schema. "
        f"Write all prose fields in {lang}. Keep technical identifiers (metric names, topics, code) as-is."
    )})
    return [{"role": "user", "content": content}]


def analyze_with_claude(text_blocks: list[tuple[str, str]], plots: list[Path], cfg: dict) -> dict:
    try:
        import anthropic
    except ImportError as exc:
        raise RuntimeError("Потрібен пакет anthropic: pip install anthropic") from exc

    client = anthropic.Anthropic()
    request = dict(
        model=cfg.get("model", "claude-opus-5-5"),
        max_tokens=int(cfg.get("max_tokens", 32000)),
        system=[{"type": "text", "text": SYSTEM_PROMPT, "cache_control": {"type": "ephemeral"}}],
        thinking={"type": "adaptive"},
        output_config={
            "effort": cfg.get("effort", "high"),
            "format": {"type": "json_schema", "schema": REPORT_SCHEMA},
        },
        messages=build_messages(text_blocks, plots, cfg.get("language", "uk")),
        # Server-side fallback: класифікатор безпеки може помилково відхилити запит (напр. через
        # тематику РЕБ) — тоді API сам перезапускає його на рекомендованій резервній моделі.
        betas=["server-side-fallback-2026-07-01"],
        fallbacks="default",
    )

    def _stream(req: dict):
        with client.beta.messages.stream(**req) as stream:
            return stream.get_final_message()

    try:
        try:
            message = _stream(request)
        except TypeError:
            # Старіша версія SDK ще не знає параметра fallbacks — передаємо його в тілі запиту.
            req = dict(request)
            req["extra_body"] = {"fallbacks": req.pop("fallbacks")}
            message = _stream(req)
    except anthropic.AuthenticationError as exc:
        raise RuntimeError("Claude API: немає дійсного ключа (ANTHROPIC_API_KEY або `ant auth login`)") from exc
    except anthropic.RateLimitError as exc:
        raise RuntimeError("Claude API: перевищено ліміт запитів — повторіть `uavlab analyze` пізніше") from exc
    except anthropic.APIStatusError as exc:
        raise RuntimeError(f"Claude API помилка {exc.status_code}: {exc.message}") from exc
    except anthropic.APIConnectionError as exc:
        raise RuntimeError(f"Claude API: немає з'єднання ({exc})") from exc

    if message.stop_reason == "refusal":
        details = getattr(message, "stop_details", None)
        raise RuntimeError(f"Claude відхилив запит (refusal): {details}")
    if message.stop_reason == "max_tokens":
        raise RuntimeError("Відповідь обрізано по max_tokens — збільште analysis.max_tokens")

    text = next((b.text for b in message.content if b.type == "text"), None)
    if not text:
        raise RuntimeError(f"Порожня відповідь Claude (stop_reason={message.stop_reason})")
    report = json.loads(text)

    usage = message.usage
    report["_meta"] = {
        "model": message.model,
        "input_tokens": usage.input_tokens,
        "output_tokens": usage.output_tokens,
        "cache_read_input_tokens": getattr(usage, "cache_read_input_tokens", None),
        "stop_reason": message.stop_reason,
    }
    return report
