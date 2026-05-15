from __future__ import annotations

import json
import logging
from typing import Optional

import numpy as np
import zmq

logger = logging.getLogger(__name__)

# Default addresses.
# Process A (main loop) connects PUSH, binds PULL.
# Process B (worker)   binds   PULL, connects PUSH.
PUSH_ADDRESS: str = "tcp://localhost:5600"  # Process A → Process B (crops)
PULL_ADDRESS: str = "tcp://*:5601"          # Process B → Process A (results)


class FramePusher:
    """
    PUSH socket — sends numpy crops to the classification worker.
    Always non-blocking: drops the frame silently if the queue is full.
    """

    def __init__(self, address: str = PUSH_ADDRESS) -> None:
        self._ctx    = zmq.Context.instance()
        self._socket: zmq.Socket = self._ctx.socket(zmq.PUSH)
        # Drop frames when HWM is reached instead of blocking.
        self._socket.setsockopt(zmq.SNDHWM, 1)
        self._socket.connect(address)
        logger.debug("FramePusher connected to %s", address)

    def send_nowait(self, frame: np.ndarray) -> bool:
        """
        Serialises frame as [metadata_json, raw_bytes] and pushes it.
        Returns True on success, False when the socket would block.
        """
        meta = json.dumps(
            {"shape": list(frame.shape), "dtype": str(frame.dtype)}
        ).encode()
        try:
            self._socket.send_multipart([meta, frame.tobytes()], flags=zmq.NOBLOCK)
            return True
        except zmq.Again:
            logger.debug("FramePusher: queue full, frame dropped")
            return False

    def close(self) -> None:
        self._socket.close()


class ResultPuller:
    """
    PULL socket — receives JSON classification results from the worker.
    Always non-blocking: returns None immediately when nothing is available.
    """

    def __init__(self, address: str = PULL_ADDRESS) -> None:
        self._ctx    = zmq.Context.instance()
        self._socket: zmq.Socket = self._ctx.socket(zmq.PULL)
        self._socket.bind(address)
        logger.debug("ResultPuller bound on %s", address)

    def try_recv(self) -> Optional[dict]:
        """
        Returns a dict with 'size_mm' and 'kalman_q_variance' if a result is
        waiting, or None when the queue is empty.
        """
        try:
            msg: dict = self._socket.recv_json(flags=zmq.NOBLOCK)
            # Validate required keys before returning to caller.
            size_mm  = float(msg["size_mm"])
            q        = float(msg["kalman_q_variance"])
            logger.debug(
                "ResultPuller: class=%-22s  size=%.1f mm  Q=%.2f",
                msg.get("drone_class", "?"), size_mm, q,
            )
            return msg
        except zmq.Again:
            return None
        except (KeyError, TypeError, ValueError) as exc:
            logger.warning("ResultPuller: malformed message — %s", exc)
            return None

    def close(self) -> None:
        self._socket.close()


class IPCCommunicator:
    """
    Facade that combines FramePusher + ResultPuller into a single object
    compatible with the IIPC protocol expected by CameraTickProcessor.
    """

    def __init__(
        self,
        push_address: str = PUSH_ADDRESS,
        pull_address: str = PULL_ADDRESS,
    ) -> None:
        self._pusher = FramePusher(push_address)
        self._puller = ResultPuller(pull_address)

    def send_nowait(self, frame: np.ndarray) -> bool:
        return self._pusher.send_nowait(frame)

    def try_recv(self) -> Optional[dict]:
        return self._puller.try_recv()

    def close(self) -> None:
        self._pusher.close()
        self._puller.close()
