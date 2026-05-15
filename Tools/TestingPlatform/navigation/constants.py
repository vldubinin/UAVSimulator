from typing import Final, TypedDict


class DroneInfo(TypedDict):
    size_mm:           float
    kalman_q_variance: float


KNOWN_DRONES_DB: Final[dict[str, DroneInfo]] = {
    "DEFAULT_AVERAGE_DRONE": {"size_mm": 350.0,   "kalman_q_variance": 5.0},
    "MAVIC_3":               {"size_mm": 347.0,   "kalman_q_variance": 2.0},
    "FPV_7_INCH":            {"size_mm": 320.0,   "kalman_q_variance": 15.0},
    "HEAVY_BOMBER":          {"size_mm": 800.0,   "kalman_q_variance": 0.5},
    "CESSNA_172":            {"size_mm": 11000.0, "kalman_q_variance": 0.1},
}

DEFAULT_PHYSICAL_SIZE_MM:  Final[float] = KNOWN_DRONES_DB["DEFAULT_AVERAGE_DRONE"]["size_mm"]
DEFAULT_KALMAN_Q_VARIANCE: Final[float] = KNOWN_DRONES_DB["DEFAULT_AVERAGE_DRONE"]["kalman_q_variance"]

FOCAL_LENGTH_PX: Final[float] = 320.0   # 640×480, HFOV=90° → f = width/2

IPC_DISPATCHER_INTERVAL_FRAMES: Final[int] = 5
REDETECT_INTERVAL_FRAMES:        Final[int] = 15   # re-run YOLO during tracking to correct bbox scale
CROP_PADDING_PX:                 Final[int] = 20
