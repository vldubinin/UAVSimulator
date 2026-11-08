"""
Порт наявного GPS-автопілота (custom_objects_dataset/gps_autopilote/autopilote.py) на стенд:
логіка Autopilote не змінена — тут лише адаптер «Observation -> compute_command -> Command»
і перемикання точок маршруту. Позиція береться з СЕНСОРА drone_geo_position (як у
uav_fly_test.py), а не з ground truth, тож у конфігу можна ставити use_truth: false.

    uavlab run examples/waypoints_gps.py -c test_configs/waypoints_gps.yaml
"""

import math
import sys
from pathlib import Path

from uavlab import Command, Controller

GPS_DIR = Path(__file__).resolve().parents[2] / "custom_objects_dataset" / "gps_autopilote"
sys.path.append(str(GPS_DIR))  # append: копії autopilote.py поруч (uavlab improve) мають пріоритет
from autopilote import Autopilote  # noqa: E402
from common import latlon_to_local_m  # noqa: E402


class WaypointsGps(Controller):
    rate_hz = 20

    def on_start(self, ctx):
        self.waypoints = [tuple(wp) for wp in ctx.params.get("waypoints", [])]
        self.radius_m = float(ctx.params.get("waypoint_radius_m", 100.0))
        self.index = 0
        self.autopilot = Autopilote()
        if not self.waypoints:
            raise ValueError("controller_params.waypoints порожній")

    def step(self, obs):
        geo = obs.get("drone_geo_position")
        position = (geo["latitude"], geo["longitude"], geo["altitude_m"]) if isinstance(geo, dict) else None

        if position is not None and self.index < len(self.waypoints):
            east, north = latlon_to_local_m(*self.waypoints[self.index][:2], position[0], position[1])
            if math.hypot(east, north) <= self.radius_m:
                self.ctx.event("waypoint_reached", index=self.index, t=obs.t)
                self.index += 1

        # Скільки точок уже взято (0..N) — логується ДО раннього виходу, щоб успіх (N) був видно в метриках
        # (controller_logs.waypoints_reached.max) і щоб stop.progress_key бачив останню взяту точку.
        self.ctx.log("waypoints_reached", self.index)
        if self.index >= len(self.waypoints):
            return Command(thrust=0.55)

        cmd = self.autopilot.compute_command(position, self.waypoints[self.index])
        self.ctx.log("waypoint_index", self.index)
        self.ctx.log("status", cmd.status)
        self.ctx.log("dist_to_wp_m", cmd.dist_m)
        self.ctx.log("est_heading_deg", cmd.heading_deg)
        self.ctx.log("bearing_deg", cmd.bearing_deg)
        self.ctx.log("has_fix", position is not None)
        return Command(cmd.roll_deg, cmd.pitch_deg, cmd.yaw_rate_deg_s, cmd.thrust)

    def is_done(self, obs):
        return self.index >= len(self.waypoints)
