"""
Bench — головний цикл стенда:

  1. зв'язок з Unreal (PING на :5557) -> START (start: auto) або очікування session_start (start: wait);
  2. маніфест умов тесту (GET_MANIFEST) -> manifest.json, ctx.manifest;
  3. цикл: вичитати шину (:5555) -> записати truth/сенсори -> з частотою rate_hz викликати
     controller.step(obs) -> PUSH команди (:5556) -> записати крок;
  4. зупинка: session_end з Unreal (Q у меню) | критерій стенда (час, аварія, дистанція, висота,
     втрата телеметрії, виняток алгоритму, is_done) | Ctrl+C  -> STOP у Unreal;
  5. фіналізація запису -> автоматичний аналіз (метрики + Claude) -> report.html.
"""

from __future__ import annotations

import time
import traceback
from pathlib import Path
from typing import Optional

import zmq

from .controller import Command, Context, load_controller
from .observation import BINARY_TOPICS, Observation, Truth, parse_payload
from .recorder import SessionRecorder
from .stop_criteria import StopCriteria
from .transport import CommandClient, SensorBusClient, SessionClient, SessionUnavailable

TRUTH_TOPIC = "flight_truth"


def _log(msg: str) -> None:
    print(f"[uavlab] {msg}", flush=True)


def _to_command(value) -> Optional[Command]:
    if value is None or isinstance(value, Command):
        return value
    if isinstance(value, dict):
        return Command(**value)
    if isinstance(value, (tuple, list)) and len(value) == 4:
        return Command(*value)
    raise TypeError(f"step() має повертати Command, dict, (roll, pitch, yaw_rate, thrust) або None, а не {type(value).__name__}")


class _Waiter:
    """Очікування старту сесії: або через UTestSessionSubsystem, або (fallback) за першим пакетом шини."""

    def __init__(self, bus: SensorBusClient, session: SessionClient, has_session: bool) -> None:
        self.bus, self.session, self.has_session = bus, session, has_session
        self.buffered_events: list[dict] = []

    def wait_for_session_start(self) -> Optional[str]:
        _log("Очікування старту: натисніть Start у меню Unreal (Ctrl+C — скасувати)...")
        while True:
            if self.has_session:
                for ev in self.session.poll_events():
                    self.buffered_events.append(ev)
                    if ev.get("type") == "session_start":
                        return ev.get("session_id")
                time.sleep(0.05)
            else:
                for _, payloads in self.bus.receive_all(200):
                    if TRUTH_TOPIC in payloads:
                        return None


def run_test(controller_path: str | Path, cfg: dict) -> Path:
    controller_path = Path(controller_path).resolve()
    controller = load_controller(controller_path, cfg.get("controller_class"))
    ep = cfg["endpoints"]

    zctx = zmq.Context()
    bus = SensorBusClient(zctx, ep["sensors"])
    commands = CommandClient(zctx, ep["commands"])
    session = SessionClient(zctx, ep["control"], ep["events"])

    _log(f"Тест: {cfg['name']}  |  алгоритм: {controller_path.name} ({type(controller).__name__})")

    # ── 1. Зв'язок і старт ────────────────────────────────────────────────
    ping = session.try_request("PING")
    has_session = bool(ping and ping.get("ok"))
    if not has_session:
        if cfg["start"] == "auto":
            raise SystemExit(
                "[uavlab] Unreal не відповідає на канал керування сесією "
                f"({ep['control']}). Запустіть PIE/гру з UAVSimulator (UTestSessionSubsystem) "
                "або вкажіть start: wait, щоб стартувати вручну з меню."
            )
        _log("УВАГА: канал сесії недоступний — режим сумісності (без маніфесту, старт/стоп лише вручну).")

    waiter = _Waiter(bus, session, has_session)
    session_id: Optional[str] = None
    status = session.try_request("STATUS") if has_session else None

    if status and status.get("running"):
        session_id = status.get("session_id")
        _log(f"Симуляція вже запущена — під'єднуюсь до сесії {session_id}")
    elif cfg["start"] == "auto":
        fields = {"mode": cfg.get("mode")}
        if cfg.get("scenario_slot"):
            fields["scenario_slot"] = cfg["scenario_slot"]
        resp = session.request("START", **fields)
        if not resp.get("ok"):
            raise SystemExit(f"[uavlab] Unreal відхилив START: {resp.get('error')}")
        session_id = resp.get("session_id")
        _log(f"Симуляцію запущено: режим {resp.get('mode')}, сесія {session_id}")
        if not resp.get("autopilot_active"):
            _log("УВАГА: автопілот (UAttitudeControlComponent) не активний — команди не впливатимуть на літак. "
                 "Використовуйте режим AutoTrack або PlaybackAndAutoTrack.")
    else:
        session_id = waiter.wait_for_session_start()

    manifest: dict = {}
    if has_session:
        resp = session.try_request("GET_MANIFEST")
        manifest = (resp or {}).get("manifest", {}) if resp and resp.get("ok") else {}
    session_id = session_id or time.strftime("%Y%m%d_%H%M%S_local")

    rec_cfg = cfg["record"]
    recorder = SessionRecorder(
        Path(rec_cfg["sessions_dir"]), f"{session_id}_{controller_path.stem}", cfg, controller_path,
        sensors_max_hz=rec_cfg.get("sensors_max_hz", 10.0), record_sensors=rec_cfg.get("sensors", True),
        extra_sources=rec_cfg.get("extra_sources") or [],
    )
    recorder.write_manifest(manifest)
    for ev in waiter.buffered_events:
        recorder.add_event(ev)
    _log(f"Запис сесії: {recorder.dir.resolve()}")

    ctx = Context(manifest=manifest, params=dict(cfg.get("controller_params") or {}),
                  session_id=session_id, session_dir=recorder.dir)
    controller.ctx = ctx

    stop = StopCriteria(cfg["stop"])
    rate_hz = float(controller.rate_hz or cfg["control_rate_hz"])
    period = 1.0 / max(rate_hz, 0.1)
    use_truth = bool(cfg.get("use_truth", True))

    stop_reason: Optional[str] = None
    unreal_stopped = False
    step_errors = collisions = 0
    started_wall = time.time()
    first_t: Optional[float] = None
    last_t: Optional[float] = None

    try:
        controller.on_start(ctx)
        recorder.add_event({"type": "bench", "kind": "test_started", "session_id": session_id,
                            "rate_hz": rate_hz, "use_truth": use_truth})

        latest_payloads: dict[str, bytes] = {}
        sensor_times: dict[str, float] = {}
        latest_truth: Optional[dict] = None
        latest_bus_time: Optional[float] = None
        new_data = False
        last_bus_wall = time.monotonic()
        next_step = time.monotonic()
        last_step_wall: Optional[float] = None
        last_print = 0.0
        prev_cmd: Optional[Command] = None

        while stop_reason is None:
            now = time.monotonic()
            timeout_ms = int(max(0.0, min(0.05, next_step - now)) * 1000)

            # ── шина сенсорів ──────────────────────────────────────────────
            for envelope, payloads in bus.receive_all(timeout_ms):
                bus_time = float(envelope.get("timestamp", 0.0))
                wall = time.time()
                last_bus_wall = time.monotonic()
                for topic, payload in payloads.items():
                    latest_payloads[topic] = payload
                    sensor_times[topic] = bus_time
                    if topic == TRUTH_TOPIC:
                        latest_truth = parse_payload(topic, payload)
                        if isinstance(latest_truth, dict):
                            recorder.add_truth(bus_time, wall, latest_truth)
                    elif topic not in BINARY_TOPICS:
                        recorder.add_sensor(topic, bus_time, parse_payload(topic, payload))
                latest_bus_time = bus_time
                first_t = bus_time if first_t is None else first_t
                last_t = bus_time
                new_data = True

            # ── події сесії з Unreal ───────────────────────────────────────
            if has_session:
                for ev in session.poll_events():
                    recorder.add_event(ev)
                    if ev.get("type") == "session_end":
                        stop_reason = f"unreal:{ev.get('reason', 'stop')}"
                        unreal_stopped = True
                    elif ev.get("kind") == "collision":
                        collisions += 1
                        _log(f"Зіткнення з {ev.get('other_actor')} (швидкість {ev.get('speed_mps', 0):.1f} м/с)")
                        if stop.on_crash:
                            stop_reason = "crash"
            if stop_reason:
                break

            if time.monotonic() - last_bus_wall > stop.telemetry_timeout_s:
                stop_reason = "telemetry_lost"
                break
            if latest_bus_time is None:
                continue

            truth_obj = Truth(latest_truth) if isinstance(latest_truth, dict) else None
            reason = stop.check_truth(latest_bus_time, truth_obj)
            if reason:
                stop_reason = reason
                break

            # ── крок алгоритму ─────────────────────────────────────────────
            now = time.monotonic()
            if now < next_step or not new_data:
                continue
            next_step = max(next_step + period, now)
            new_data = False

            obs = Observation(
                t=latest_bus_time,
                wall_time=time.time(),
                sensors={t: parse_payload(t, p) for t, p in latest_payloads.items() if t != TRUTH_TOPIC or use_truth},
                truth=truth_obj if use_truth else None,
                sensor_times=dict(sensor_times),
            )

            t0 = time.perf_counter()
            cmd: Optional[Command] = None
            done = False
            try:
                cmd = _to_command(controller.step(obs))
                done = bool(controller.is_done(obs))
            except Exception:
                step_errors += 1
                tb = traceback.format_exc()
                _log(f"Виняток у step():\n{tb}")
                recorder.add_event({"type": "bench", "kind": "controller_exception", "sim_time": obs.t, "traceback": tb})
                if step_errors >= stop.max_step_errors:
                    stop_reason = "controller_exception"
            latency_ms = (time.perf_counter() - t0) * 1000.0

            sent = commands.send(cmd) if cmd is not None else False
            logs, ctrl_events = ctx._take()
            for e in ctrl_events:
                recorder.add_event({"type": "controller", "sim_time": obs.t, **e})

            row = {
                "sim_time": obs.t,
                "wall_time": obs.wall_time,
                "dt_wall": (now - last_step_wall) if last_step_wall is not None else None,
                "step_latency_ms": latency_ms,
                "cmd_sent": sent,
                "cmd_held": cmd is None,
            }
            effective = cmd or prev_cmd
            if effective is not None:
                row.update({"cmd.roll_deg": effective.roll_deg, "cmd.pitch_deg": effective.pitch_deg,
                            "cmd.yaw_rate_deg_s": effective.yaw_rate_deg_s, "cmd.thrust": effective.thrust})
            for k, v in logs.items():
                row[f"log.{k}"] = v if isinstance(v, (int, float, str, bool)) or v is None else str(v)
            recorder.add_step(row)
            last_step_wall = now
            prev_cmd = effective

            if done and stop_reason is None:
                stop_reason = "goal_reached"

            if now - last_print >= 1.0 and truth_obj is not None:
                last_print = now
                c = effective or Command()
                _log(f"t={stop.elapsed(obs.t):6.1f}s  alt={truth_obj.alt_m or 0:7.1f}m  "
                     f"V={truth_obj.airspeed_mps:5.1f}м/с  roll={truth_obj.roll_deg:6.1f}°  "
                     f"pitch={truth_obj.pitch_deg:6.1f}°  | cmd roll={c.roll_deg:6.1f} pitch={c.pitch_deg:6.1f} "
                     f"yaw_rate={c.yaw_rate_deg_s:5.1f} thr={c.thrust:4.2f}")

    except KeyboardInterrupt:
        stop_reason = stop_reason or "user_interrupt"
    finally:
        stop_reason = stop_reason or "unknown"
        _log(f"Зупинка тесту: {stop_reason}")
        if has_session and not unreal_stopped:
            try:
                session.request("STOP", reason=stop_reason)
            except SessionUnavailable as exc:
                _log(f"Не вдалося зупинити симуляцію в Unreal: {exc}")
        try:
            controller.on_stop(ctx)
        except Exception:
            recorder.add_event({"type": "bench", "kind": "on_stop_exception", "traceback": traceback.format_exc()})
        recorder.add_event({"type": "bench", "kind": "test_stopped", "reason": stop_reason})
        summary = {
            "session_id": session_id,
            "test_name": cfg["name"],
            "objective": cfg.get("objective", ""),
            "controller_file": controller_path.name,
            "controller_class": type(controller).__name__,
            "stop_reason": stop_reason,
            "sim_duration_s": (last_t - first_t) if first_t is not None and last_t is not None else 0.0,
            "wall_duration_s": time.time() - started_wall,
            "truth_samples": recorder.truth_count,
            "controller_steps": recorder.step_count,
            "control_rate_hz": rate_hz,
            "use_truth": use_truth,
            "collisions": collisions,
            "controller_exceptions": step_errors,
            "session_channel": has_session,
        }
        recorder.finalize(summary)
        bus.close()
        commands.close()
        session.close()
        zctx.term()

    _log(f"Сесію збережено: {recorder.dir.resolve()}  ({recorder.truth_count} пакетів truth, {recorder.step_count} кроків)")

    if cfg["analysis"].get("enabled", True):
        from .analysis import analyze_session
        try:
            analyze_session(recorder.dir, cfg["analysis"])
        except Exception:
            _log("Аналіз не вдався (сесію збережено, повторіть: uavlab analyze <тека сесії>):\n" + traceback.format_exc())
    return recorder.dir
