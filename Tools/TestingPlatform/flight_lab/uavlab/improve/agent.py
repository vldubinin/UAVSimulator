"""
Агент правок: Claude з інструментами, що змінюють ЛИШЕ нову версію (копію) алгоритму.

Інструменти (вмикаються списком improve.allow):
  read_file            — прочитати файл з src/ версії (завжди доступний)
  edit_code            — точкова заміна old_str -> new_str у файлі src/   (allow: code)
  set_controller_params — змінити controller_params конфігу               (allow: params)
  set_autopilot_pid    — PID внутрішнього автопілота на час сесії          (allow: autopilot_pid)
  submit_changes       — завершити ітерацію (перед цим стенд перевіряє синтаксис та імпорт)

Мета, критерії приймання, критерії зупинки й сам тест агенту недоступні.
"""

from __future__ import annotations

import difflib
import json
import os
import py_compile
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

from .workspace import Version

PRICES_PER_MTOK = {  # $ за 1 млн токенів: (вхід, вихід, читання кешу, запис кешу)
    "claude-opus-5-5": (4.0, 20.0, 0.20, 5.0),
}
DEFAULT_PRICE = (4.0, 20.0, 0.20, 5.0)

# Додані агентом рядки не можуть містити цього: код алгоритму виконується з вашими правами.
FORBIDDEN_PATTERNS = [r"\bsubprocess\b", r"\bos\.system\b", r"\bos\.remove\b", r"\bos\.unlink\b", r"\bshutil\.rmtree\b",
                      r"\beval\s*\(", r"\bexec\s*\(", r"__import__", r"\bsocket\b", r"\burllib\b", r"\brequests\b",
                      r"\bctypes\b"]

PID_FIELDS = {
    "kp": {"type": "number"}, "ki": {"type": "number"}, "kd": {"type": "number"},
    "integral_clamp": {"type": "number", "description": "межа внеску I-складової у вихід [0..1]"},
    "derivative_filter_s": {"type": "number", "description": "стала часу НЧ-фільтра похідної, с"},
    "invert_output": {"type": "boolean"},
    "gain_schedule": {"type": "string", "enum": ["keep", "disable"],
                      "description": "disable — вимкнути криву гейн-шедулінгу (множник стане 1)"},
}

TOOLS: dict[str, dict] = {
    "read_file": {
        "name": "read_file",
        "description": "Read a Python source file of the algorithm under test (file names as listed in <sources>). Returns numbered lines.",
        "strict": True,
        "input_schema": {"type": "object", "additionalProperties": False, "required": ["file"],
                         "properties": {"file": {"type": "string"}}},
    },
    "edit_code": {
        "name": "edit_code",
        "description": ("Replace an exact snippet in one algorithm source file. old_str must match the file exactly "
                        "(including indentation, without line-number prefixes) and occur exactly once. "
                        "The edit is rejected if the file stops compiling."),
        "strict": True,
        "input_schema": {"type": "object", "additionalProperties": False, "required": ["file", "old_str", "new_str", "reason"],
                         "properties": {"file": {"type": "string"}, "old_str": {"type": "string"},
                                        "new_str": {"type": "string"}, "reason": {"type": "string"}}},
    },
    "set_controller_params": {
        "name": "set_controller_params",
        "description": ("Set entries of controller_params (exposed to the algorithm as ctx.params). "
                        "value_json is a JSON literal, e.g. \"3.5\", \"true\", \"[1, 2]\"."),
        "strict": True,
        "input_schema": {"type": "object", "additionalProperties": False, "required": ["params", "reason"],
                         "properties": {
                             "params": {"type": "array", "items": {
                                 "type": "object", "additionalProperties": False, "required": ["name", "value_json"],
                                 "properties": {"name": {"type": "string"}, "value_json": {"type": "string"}}}},
                             "reason": {"type": "string"}}},
    },
    "set_autopilot_pid": {
        "name": "set_autopilot_pid",
        "description": ("Set the simulator's inner autopilot PID for one axis for the test session (runtime only; the "
                        "Unreal Blueprint is not changed). Error units: radians (roll/pitch) or rad/s (yaw_rate); output "
                        "is a normalized control-surface command in [-1, 1]. Effective gain = K × gain-schedule multiplier. "
                        "Provide all fields; repeat current values for fields you do not change."),
        "strict": True,
        "input_schema": {"type": "object", "additionalProperties": False,
                         "required": ["axis", *PID_FIELDS.keys(), "reason"],
                         "properties": {"axis": {"type": "string", "enum": ["roll", "pitch", "yaw_rate"]},
                                        **PID_FIELDS, "reason": {"type": "string"}}},
    },
    "submit_changes": {
        "name": "submit_changes",
        "description": ("Finish this iteration. Call it once, after making your changes. Set stop_improving=true and make "
                        "no changes if you believe further changes cannot improve the result."),
        "strict": True,
        "input_schema": {"type": "object", "additionalProperties": False,
                         "required": ["summary", "hypothesis", "expected_effects", "stop_improving"],
                         "properties": {"summary": {"type": "string"}, "hypothesis": {"type": "string"},
                                        "expected_effects": {"type": "array", "items": {"type": "string"}},
                                        "stop_improving": {"type": "boolean"}}},
    },
}
ALLOW_TO_TOOL = {"code": "edit_code", "params": "set_controller_params", "autopilot_pid": "set_autopilot_pid"}

SYSTEM_PROMPT = """You are a flight-control engineer iteratively improving a fixed-wing UAV control setup in a \
closed loop: you make changes -> the test bench re-runs the same virtual flight test -> the result is scored \
against FROZEN acceptance criteria -> the change is kept only if the score improves, otherwise it is reverted.

System under test: a Python algorithm (outer loop) returns attitude setpoints (roll_deg, pitch_deg, \
yaw_rate_deg_s, thrust) at a fixed rate; the simulator's inner autopilot runs PID loops on roll angle, pitch \
angle and yaw rate (errors in radians / rad/s, outputs normalized surface commands in [-1, 1], optional \
airspeed gain-schedule multiplier). Depending on permissions you may edit the algorithm source, its \
controller_params, and/or the inner autopilot PID for the session.

Rules:
- Read the acceptance criteria, the latest evaluation, the analysis report and the iteration history first. \
Fix the root cause indicated by evidence; attribute correctly (inner PID vs. algorithm).
- Prefer the smallest change that tests one clear hypothesis per iteration, so the result is interpretable. \
Do not repeat a change that was already tried and reverted unless you change it materially.
- Change gains in reasonable steps (typically x1.5 - x3), keep stability margins in mind (overshoot, \
oscillation, saturation, load factor, AoA).
- You cannot change the objective, the acceptance criteria, the stop criteria or the test program. Never \
game the metrics (e.g. making the algorithm detect the test or stop early).
- Algorithm code must stay a deterministic controller: no file/network/process access, no new third-party \
dependencies. Keep the public structure (Controller subclass, step/on_start/is_done) intact.
- When done, call submit_changes exactly once with a short summary, the hypothesis and expected metric effects.
Write summaries and reasons in {language}."""


def _price(model: str) -> tuple:
    return PRICES_PER_MTOK.get(model, DEFAULT_PRICE)


def usage_cost(usage: dict) -> float:
    p_in, p_out, p_cr, p_cw = _price(usage.get("model", ""))
    return (usage.get("input_tokens", 0) * p_in + usage.get("output_tokens", 0) * p_out
            + usage.get("cache_read_input_tokens", 0) * p_cr + usage.get("cache_creation_input_tokens", 0) * p_cw) / 1e6


def _usage(resp) -> dict:
    u = resp.usage
    return {"model": resp.model, "input_tokens": u.input_tokens, "output_tokens": u.output_tokens,
            "cache_read_input_tokens": getattr(u, "cache_read_input_tokens", 0) or 0,
            "cache_creation_input_tokens": getattr(u, "cache_creation_input_tokens", 0) or 0}


def check_controller_loads(version: Version, extra_path: str) -> str | None:
    """Імпорт алгоритму в окремому процесі (як при реальному запуску). None — ок, інакше текст помилки."""
    code = ("import sys; from uavlab.controller import load_controller; "
            f"c = load_controller(r'{version.entry_path}', {version.config.get('controller_class')!r}); print('OK')")
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(filter(None, [extra_path, env.get("PYTHONPATH", "")]))
    try:
        r = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, timeout=60, env=env,
                           cwd=str(version.src))
    except subprocess.TimeoutExpired:
        return "імпорт алгоритму завис довше 60 с"
    return None if r.returncode == 0 and "OK" in r.stdout else (r.stderr or r.stdout)[-3000:]


class ChangeAgent:
    def __init__(self, cfg: dict, language: str = "uk") -> None:
        import anthropic

        self.client = anthropic.Anthropic()
        self.cfg = cfg
        allowed = [ALLOW_TO_TOOL[a] for a in cfg.get("allow", []) if a in ALLOW_TO_TOOL]
        self.tools = [TOOLS["read_file"], *[TOOLS[t] for t in allowed], TOOLS["submit_changes"]]
        self.system = SYSTEM_PROMPT.replace("{language}", {"uk": "Ukrainian"}.get(language, language))
        self.usages: list[dict] = []

    # ── інструменти ─────────────────────────────────────────────────────────
    def _file(self, v: Version, name: str) -> Path:
        p = (v.src / Path(name).name)
        if p.suffix != ".py" or not p.exists():
            raise ValueError(f"файл '{name}' не знайдено серед <sources> ({', '.join(v.sources())})")
        return p

    def _exec_tool(self, v: Version, name: str, args: dict, change: dict) -> str:
        if name == "read_file":
            text = self._file(v, args["file"]).read_text(encoding="utf-8", errors="replace")
            return "\n".join(f"{i:4d}| {line}" for i, line in enumerate(text.splitlines(), 1))

        if name == "edit_code":
            p = self._file(v, args["file"])
            text = p.read_text(encoding="utf-8")
            n = text.count(args["old_str"])
            if n != 1:
                raise ValueError(f"old_str зустрічається {n} раз(ів) у {p.name}, потрібно рівно 1")
            added = set(args["new_str"].splitlines()) - set(args["old_str"].splitlines())
            for line in added:
                for pat in FORBIDDEN_PATTERNS:
                    if re.search(pat, line):
                        raise ValueError(f"заборонена конструкція ({pat}) у доданому коді: {line.strip()}")
            new_text = text.replace(args["old_str"], args["new_str"])
            p.write_text(new_text, encoding="utf-8")
            try:
                py_compile.compile(str(p), doraise=True)
            except py_compile.PyCompileError as exc:
                p.write_text(text, encoding="utf-8")
                raise ValueError(f"правку відкочено — синтаксична помилка: {exc.msg}") from None
            change.setdefault("code_edits", []).append({"file": p.name, "reason": args["reason"]})
            return f"OK: {p.name} змінено."

        if name == "set_controller_params":
            params = v.config.setdefault("controller_params", {})
            done = {}
            for item in args["params"]:
                try:
                    value = json.loads(item["value_json"])
                except json.JSONDecodeError as exc:
                    raise ValueError(f"value_json для '{item['name']}' не є JSON: {exc}") from None
                params[item["name"]] = value
                done[item["name"]] = value
            v.save_config()
            change.setdefault("params", {}).update(done)
            change.setdefault("param_reasons", []).append(args["reason"])
            return f"OK: controller_params оновлено: {json.dumps(done, ensure_ascii=False)}"

        if name == "set_autopilot_pid":
            values = {k: args[k] for k in PID_FIELDS}
            if values["gain_schedule"] == "keep":
                values.pop("gain_schedule")
            v.config.setdefault("autopilot_pid", {})[args["axis"]] = values
            v.save_config()
            change.setdefault("autopilot_pid", {})[args["axis"]] = values
            change.setdefault("pid_reasons", []).append(f"{args['axis']}: {args['reason']}")
            return f"OK: PID {args['axis']} = {json.dumps(values)} (на час сесії)."

        raise ValueError(f"невідомий інструмент {name}")

    # ── цикл агента ─────────────────────────────────────────────────────────
    def propose(self, v: Version, context_blocks: list[tuple[str, str]], extra_path: str) -> dict:
        """Вносить правки у версію v. Повертає change-dict (summary, hypothesis, правки, stop_improving)."""
        content = [{"type": "text", "text": f"<{n}>\n{t}\n</{n}>"} for n, t in context_blocks]
        content.append({"type": "text", "text": "Make your changes to this new version, then call submit_changes."})
        messages: list[dict[str, Any]] = [{"role": "user", "content": content}]
        change: dict[str, Any] = {}
        before = v.sources()
        nudged = False

        for _ in range(int(self.cfg.get("max_agent_turns", 20))):
            resp = self._create(messages)
            self.usages.append(_usage(resp))
            if resp.stop_reason == "refusal":
                raise RuntimeError(f"Claude відхилив запит (refusal): {getattr(resp, 'stop_details', None)}")
            messages.append({"role": "assistant", "content": resp.content})

            tool_uses = [b for b in resp.content if b.type == "tool_use"]
            if not tool_uses:
                if nudged:
                    break
                nudged = True
                messages.append({"role": "user", "content": "Call submit_changes to finish this iteration."})
                continue

            results, submitted = [], None
            for tu in tool_uses:
                if tu.name == "submit_changes":
                    err = check_controller_loads(v, extra_path) if self._has_code_changes(v, before) else None
                    if err:
                        results.append({"type": "tool_result", "tool_use_id": tu.id, "is_error": True,
                                        "content": f"Алгоритм не завантажується — виправте:\n{err}"})
                    else:
                        submitted = tu.input
                        results.append({"type": "tool_result", "tool_use_id": tu.id, "content": "Прийнято."})
                    continue
                try:
                    out = self._exec_tool(v, tu.name, tu.input, change)
                    results.append({"type": "tool_result", "tool_use_id": tu.id, "content": out})
                except Exception as exc:  # помилка інструмента — назад агенту, хай виправляє
                    results.append({"type": "tool_result", "tool_use_id": tu.id, "is_error": True, "content": str(exc)})
            messages.append({"role": "user", "content": results})
            if submitted is not None:
                change.update(submitted)
                break
        else:
            change.setdefault("summary", "агент вичерпав ліміт кроків без submit_changes")

        change["diff"] = self._diff(before, v.sources())
        return change

    def _create(self, messages):
        req = dict(
            model=self.cfg.get("model", "claude-opus-5-5"),
            max_tokens=16000,
            system=[{"type": "text", "text": self.system, "cache_control": {"type": "ephemeral"}}],
            tools=self.tools,
            thinking={"type": "adaptive"},
            output_config={"effort": self.cfg.get("effort", "high")},
            messages=messages,
            betas=["server-side-fallback-2026-07-01"],
            fallbacks="default",
        )
        try:
            return self.client.beta.messages.create(**req)
        except TypeError:
            req["extra_body"] = {"fallbacks": req.pop("fallbacks")}  # старіший SDK
            return self.client.beta.messages.create(**req)

    @staticmethod
    def _has_code_changes(v: Version, before: dict[str, str]) -> bool:
        return v.sources() != before

    @staticmethod
    def _diff(before: dict[str, str], after: dict[str, str]) -> str:
        out = []
        for name in sorted(set(before) | set(after)):
            a, b = before.get(name, ""), after.get(name, "")
            if a != b:
                out.extend(difflib.unified_diff(a.splitlines(), b.splitlines(), f"a/{name}", f"b/{name}", lineterm=""))
        return "\n".join(out)

    @property
    def cost(self) -> float:
        return sum(usage_cost(u) for u in self.usages)
