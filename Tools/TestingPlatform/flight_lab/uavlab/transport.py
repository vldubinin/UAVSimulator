"""
ZMQ-транспорт між стендом і UAVSimulator. Порти (дефолтні):

  5555  SUB  <- USensorBusComponent (PUB)     — сенсори + flight_truth, мультипарт
  5556  PUSH -> UAttitudeControlComponent     — SET_ATTITUDE_TARGET (радіани)
  5557  REQ  <-> UTestSessionSubsystem (REP)  — PING / STATUS / GET_MANIFEST / START / STOP
  5558  SUB  <- UTestSessionSubsystem (PUB)   — session_start / session_end / event
"""

from __future__ import annotations

import json
import math
from typing import Optional

import zmq

from .controller import Command


class SensorBusClient:
    """Підписник сенсорної шини. Формат: part 0 = JSON-конверт
    {"timestamp", "sensors": [{"topic","timestamp"}, ...]}, part 1..N — payload'и в тому ж порядку."""

    def __init__(self, ctx: zmq.Context, endpoint: str) -> None:
        self._sock = ctx.socket(zmq.SUB)
        self._sock.setsockopt(zmq.RCVHWM, 50)
        self._sock.setsockopt(zmq.LINGER, 0)
        self._sock.connect(endpoint)
        self._sock.setsockopt_string(zmq.SUBSCRIBE, "")
        self._poller = zmq.Poller()
        self._poller.register(self._sock, zmq.POLLIN)

    def receive_all(self, timeout_ms: int) -> list[tuple[dict, dict[str, bytes]]]:
        """Чекає до timeout_ms на перше повідомлення, потім вичитує все, що накопичилось.
        Повертає список (envelope, {topic: payload}) у порядку надходження."""
        out: list[tuple[dict, dict[str, bytes]]] = []
        if not self._poller.poll(timeout_ms):
            return out
        while True:
            try:
                parts = self._sock.recv_multipart(zmq.NOBLOCK)
            except zmq.Again:
                break
            parsed = self._parse(parts)
            if parsed is not None:
                out.append(parsed)
        return out

    @staticmethod
    def _parse(parts: list[bytes]) -> Optional[tuple[dict, dict[str, bytes]]]:
        try:
            envelope = json.loads(parts[0].decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError, IndexError):
            return None
        payloads: dict[str, bytes] = {}
        for i, info in enumerate(envelope.get("sensors", []), start=1):
            if i >= len(parts):
                break
            payloads[info.get("topic", f"unknown_{i}")] = parts[i]
        return envelope, payloads

    def close(self) -> None:
        self._sock.close(0)


def build_attitude_command(cmd: Command) -> dict:
    """SET_ATTITUDE_TARGET — UAttitudeControlComponent чекає кути в РАДІАНАХ (див. uav_fly_test.py)."""
    return {
        "command_type": "SET_ATTITUDE_TARGET",
        "roll": math.radians(cmd.roll_deg),
        "pitch": math.radians(cmd.pitch_deg),
        "yaw_rate": math.radians(cmd.yaw_rate_deg_s),
        "thrust": float(min(1.0, max(0.0, cmd.thrust))),
    }


class CommandClient:
    def __init__(self, ctx: zmq.Context, endpoint: str) -> None:
        self._sock = ctx.socket(zmq.PUSH)
        self._sock.setsockopt(zmq.SNDHWM, 1)
        self._sock.setsockopt(zmq.LINGER, 0)
        self._sock.connect(endpoint)

    def send(self, cmd: Command) -> bool:
        try:
            self._sock.send_string(json.dumps(build_attitude_command(cmd)), zmq.NOBLOCK)
            return True
        except zmq.Again:
            return False  # Unreal ще не прив'язав PULL (автопілот не активовано) — команда втрачена

    def close(self) -> None:
        self._sock.close(0)


class SessionUnavailable(RuntimeError):
    pass


class SessionClient:
    """REQ/REP-канал керування сесією + SUB подій. REQ перестворюється після таймауту
    ("lazy pirate"), бо REQ-сокет без відповіді застрягає в стані очікування."""

    def __init__(self, ctx: zmq.Context, control_endpoint: str, events_endpoint: str,
                 timeout_ms: int = 2000) -> None:
        self._ctx = ctx
        self._control_endpoint = control_endpoint
        self._timeout_ms = timeout_ms
        self._req: Optional[zmq.Socket] = None
        self._open_req()

        self._events = ctx.socket(zmq.SUB)
        self._events.setsockopt(zmq.LINGER, 0)
        self._events.connect(events_endpoint)
        self._events.setsockopt_string(zmq.SUBSCRIBE, "")

    def _open_req(self) -> None:
        if self._req is not None:
            self._req.close(0)
        self._req = self._ctx.socket(zmq.REQ)
        self._req.setsockopt(zmq.LINGER, 0)
        self._req.setsockopt(zmq.RCVTIMEO, self._timeout_ms)
        self._req.setsockopt(zmq.SNDTIMEO, self._timeout_ms)
        self._req.connect(self._control_endpoint)

    def request(self, command: str, **fields) -> dict:
        try:
            self._req.send_string(json.dumps({"command": command, **fields}))
            return json.loads(self._req.recv_string())
        except (zmq.Again, zmq.ZMQError) as exc:
            self._open_req()
            raise SessionUnavailable(
                f"UTestSessionSubsystem не відповідає на {self._control_endpoint} ({command}): {exc}"
            ) from exc

    def try_request(self, command: str, **fields) -> Optional[dict]:
        try:
            return self.request(command, **fields)
        except SessionUnavailable:
            return None

    def poll_events(self) -> list[dict]:
        events: list[dict] = []
        while True:
            try:
                raw = self._events.recv_string(zmq.NOBLOCK)
            except zmq.Again:
                break
            try:
                events.append(json.loads(raw))
            except json.JSONDecodeError:
                pass
        return events

    def close(self) -> None:
        if self._req is not None:
            self._req.close(0)
        self._events.close(0)
