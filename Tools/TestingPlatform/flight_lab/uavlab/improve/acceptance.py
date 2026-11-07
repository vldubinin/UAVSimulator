"""
Формальні критерії приймання — єдине, за чим `uavlab improve` вирішує «краще / гірше / пройдено».

Критерій: {"metric": "attitude_tracking.roll.rms_error", "max": 3.0}  (або "min": ...).
`metric` — шлях через крапку в metrics.json. Критерії фіксуються ДО першої правки і агент
не може їх змінити — інакше найпростіше «виправлення» було б послабити вимоги.
"""

from __future__ import annotations

import json
import math
from typing import Any, Optional

# Причини зупинки, після яких версія вважається проваленою незалежно від метрик.
HARD_FAIL_REASONS = {"crash", "controller_exception", "telemetry_lost", "min_altitude", "max_distance",
                     "user_interrupt", "unknown"}


def get_metric(metrics: dict, path: str) -> Optional[float]:
    node: Any = metrics
    for key in path.split("."):
        if isinstance(node, dict) and key in node:
            node = node[key]
        elif isinstance(node, list) and key.isdigit() and int(key) < len(node):
            node = node[int(key)]
        else:
            return None
    if isinstance(node, bool):
        return float(node)
    if isinstance(node, (int, float)) and math.isfinite(float(node)):
        return float(node)
    return None


def numeric_metric_paths(metrics: dict, prefix: str = "") -> dict[str, float]:
    """Усі числові листки metrics.json (без списків) — меню для генерації критеріїв."""
    out: dict[str, float] = {}
    for k, v in metrics.items():
        path = f"{prefix}{k}"
        if isinstance(v, dict):
            out.update(numeric_metric_paths(v, path + "."))
        elif isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(float(v)):
            out[path] = float(v)
    return out


def evaluate(criteria: list[dict], metrics: dict, summary: dict) -> dict:
    details, passed, violation = [], 0, 0.0
    for c in criteria:
        value = get_metric(metrics, c["metric"])
        op = "max" if "max" in c else "min"
        thr = float(c[op])
        if value is None:
            ok, excess = False, 1.0  # метрики немає (напр. ступінь не досяг 90 %) — повний провал критерію
        else:
            diff = (value - thr) if op == "max" else (thr - value)
            ok = diff <= 0
            excess = max(0.0, diff) / max(abs(thr), 1e-6)
        passed += ok
        violation += min(excess, 10.0)
        details.append({"metric": c["metric"], "op": op, "threshold": thr, "value": value, "ok": ok,
                        "excess": round(excess, 4)})

    reason = str(summary.get("stop_reason", ""))
    hard_fail = reason if (reason in HARD_FAIL_REASONS or reason.startswith("unreal:user")) else None
    return {
        "passed": passed,
        "total": len(criteria),
        "all_passed": passed == len(criteria) and hard_fail is None,
        "violation": round(violation, 4),
        "hard_fail": hard_fail,
        "details": details,
    }


def score(ev: dict) -> tuple:
    """Більше = краще: (без жорсткого провалу, к-сть пройдених критеріїв, -сумарне перевищення)."""
    return (0 if ev.get("hard_fail") else 1, ev["passed"], -ev["violation"])


def is_better(candidate: dict, best: dict, min_violation_gain: float = 0.02) -> bool:
    a, b = score(candidate), score(best)
    if a[:2] != b[:2]:
        return a[:2] > b[:2]
    # За рівної к-сті пройдених — вимагаємо відчутного зменшення перевищень (шум між прогонами).
    return (b[2] - a[2]) < -min_violation_gain * max(1.0, abs(b[2]))


CRITERIA_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["criteria", "notes"],
    "properties": {
        "criteria": {
            "type": "array",
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["metric", "op", "threshold", "rationale"],
                "properties": {
                    "metric": {"type": "string"},
                    "op": {"type": "string", "enum": ["max", "min"]},
                    "threshold": {"type": "number"},
                    "rationale": {"type": "string"},
                },
            },
        },
        "notes": {"type": "string"},
    },
}


def generate_criteria(objective: str, metrics: dict, cfg: dict) -> tuple[list[dict], str, dict]:
    """Перетворює словесну мету тесту на формальні критерії (Claude, structured output).
    Повертає (criteria, notes, usage)."""
    import anthropic

    paths = numeric_metric_paths(metrics)
    menu = "\n".join(f"{p} = {v:g}" for p, v in sorted(paths.items()))
    prompt = (
        "Convert the flight-test objective into machine-checkable acceptance criteria over the metrics below.\n"
        "Rules: use ONLY metric paths from the list (exact spelling); 2-8 criteria; thresholds must follow the "
        "objective literally (do not relax or tighten it); prefer aggregate metrics (…_max, …_min, rms_error) "
        "over single examples; if the objective cannot be expressed with the available metrics, say so in notes.\n"
        "Notes on metrics: *_step_response.t90_s_max is null when any step never reached 90% (then the criterion fails); "
        "reached_fraction_min is the worst fraction of a step reached; overshoot in percent; angles in degrees.\n\n"
        f"<objective>\n{objective}\n</objective>\n\n<available_metrics current_values>\n{menu}\n</available_metrics>"
    )
    client = anthropic.Anthropic()
    msg = client.messages.create(
        model=cfg.get("model", "claude-opus-5-5"),
        max_tokens=8000,
        thinking={"type": "adaptive"},
        output_config={"effort": "medium", "format": {"type": "json_schema", "schema": CRITERIA_SCHEMA}},
        messages=[{"role": "user", "content": prompt}],
    )
    if msg.stop_reason == "refusal":
        raise RuntimeError("Claude відхилив генерацію критеріїв")
    data = json.loads(next(b.text for b in msg.content if b.type == "text"))
    criteria = [{"metric": c["metric"], c["op"]: c["threshold"], "rationale": c["rationale"]}
                for c in data["criteria"] if c["metric"] in paths or _is_known_nullable(c["metric"], metrics)]
    return criteria, data.get("notes", ""), _usage(msg)


def _is_known_nullable(path: str, metrics: dict) -> bool:
    """Шлях існує в metrics.json, але зараз має значення null (напр. t90_s_max)."""
    node: Any = metrics
    for key in path.split("."):
        if not isinstance(node, dict) or key not in node:
            return False
        node = node[key]
    return node is None


def _usage(msg) -> dict:
    u = msg.usage
    return {"model": msg.model, "input_tokens": u.input_tokens, "output_tokens": u.output_tokens,
            "cache_read_input_tokens": getattr(u, "cache_read_input_tokens", 0) or 0,
            "cache_creation_input_tokens": getattr(u, "cache_creation_input_tokens", 0) or 0}
