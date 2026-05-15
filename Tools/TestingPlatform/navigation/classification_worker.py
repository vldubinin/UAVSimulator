"""
classification_worker.py — Process B (background classifier).

Runs as a standalone process. Receives crop frames from Process A via ZMQ PULL,
simulates heavy inference, and returns a JSON result via ZMQ PUSH.

Socket layout (mirrors ipc_communicator.py):
  PULL binds  tcp://*:5555   ← Process A PUSH connects here  (crops inbound)
  PUSH connects tcp://localhost:5556 → Process A PULL binds there (results outbound)

Usage:
  python classification_worker.py            # always returns CESSNA_172 (test mode)
"""
from __future__ import annotations

import json
import logging
import time

import numpy as np
import zmq

from constants import KNOWN_DRONES_DB

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [WORKER] %(levelname)s  %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger(__name__)

WORKER_PULL_ADDRESS: str = "tcp://*:5600"
WORKER_PUSH_ADDRESS: str = "tcp://localhost:5601"

SIMULATED_INFERENCE_DELAY_S: float = 0.5


# ── Frame deserialisation ─────────────────────────────────────────────────────

def _decode_frame(parts: list[bytes]) -> np.ndarray:
    """Reconstruct numpy array from [meta_json, raw_bytes] multipart message."""
    meta  = json.loads(parts[0])
    array = np.frombuffer(parts[1], dtype=meta["dtype"]).reshape(meta["shape"]).copy()
    return array


# ── Queue drain — keep only the freshest frame ───────────────────────────────

def _recv_latest(socket: zmq.Socket) -> np.ndarray:
    """
    Wait for a frame, then drain stale ones. Polls with a timeout so
    Ctrl+C (SIGINT) can interrupt the loop between poll intervals.
    """
    while True:
        if not socket.poll(timeout=500):     # yields to Python signal handler every 500 ms
            continue

        parts = socket.recv_multipart()
        discarded = 0

        while True:                          # drain without blocking
            try:
                parts = socket.recv_multipart(flags=zmq.NOBLOCK)
                discarded += 1
            except zmq.Again:
                break

        if discarded:
            logger.debug("Drained %d stale frame(s) from queue", discarded)

        return _decode_frame(parts)


# ── Classification stub ───────────────────────────────────────────────────────

_TEST_CLASS = "CESSNA_172"


def _classify(frame: np.ndarray) -> dict:  # noqa: ARG001
    """
    STUB: simulates inference delay, always reports CESSNA_172 for testing.
    Replace this function body with real model inference when the model is ready.
    """
    time.sleep(SIMULATED_INFERENCE_DELAY_S)

    info = KNOWN_DRONES_DB[_TEST_CLASS]
    return {
        "drone_class":      _TEST_CLASS,
        "size_mm":          info["size_mm"],
        "kalman_q_variance": info["kalman_q_variance"],
    }


# ── Main loop ─────────────────────────────────────────────────────────────────

def run() -> None:
    ctx = zmq.Context()

    puller: zmq.Socket = ctx.socket(zmq.PULL)
    puller.bind(WORKER_PULL_ADDRESS)
    logger.info("PULL socket bound on %s", WORKER_PULL_ADDRESS)

    pusher: zmq.Socket = ctx.socket(zmq.PUSH)
    pusher.connect(WORKER_PUSH_ADDRESS)
    logger.info("PUSH socket connected to %s", WORKER_PUSH_ADDRESS)

    logger.info("Worker ready  [TEST MODE — always classifies as %s]", _TEST_CLASS)

    try:
        while True:
            frame  = _recv_latest(puller)
            logger.debug("Received crop shape=%s dtype=%s", frame.shape, frame.dtype)

            result = _classify(frame)
            pusher.send_json(result)

            logger.info(
                "Classified → class=%-12s  size=%.1f mm  Q=%.2f",
                result["drone_class"],
                result["size_mm"],
                result["kalman_q_variance"],
            )

    except KeyboardInterrupt:
        logger.info("Worker shutting down (KeyboardInterrupt)")
    finally:
        puller.close()
        pusher.close()
        ctx.term()
        logger.info("ZMQ context terminated. Bye.")


if __name__ == "__main__":
    run()
