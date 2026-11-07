"""
Мінімальний приклад: утримання висоти й курсу (outer loop над внутрішнім PID автопілота).

    uavlab run examples/hold_altitude.py -c test_configs/hold_altitude.yaml
"""

from uavlab import Command, Controller


def wrap180(a: float) -> float:
    return (a + 180.0) % 360.0 - 180.0


class HoldAltitude(Controller):
    rate_hz = 20

    def on_start(self, ctx):
        p = ctx.params
        self.target_alt = p.get("target_alt_m")         # None -> висота на старті
        self.target_heading = p.get("target_heading_deg")  # None -> курс на старті
        self.kp_alt = p.get("kp_alt_deg_per_m", 0.3)
        self.kp_hdg = p.get("kp_heading_roll_per_deg", 0.8)
        self.max_pitch = p.get("max_pitch_deg", 12.0)
        self.max_roll = p.get("max_roll_deg", 25.0)
        self.thrust = p.get("thrust", 0.7)

    def step(self, obs):
        tr = obs.truth
        if tr is None or tr.alt_m is None:
            return Command(thrust=self.thrust)  # даних ще немає — рівний політ

        if self.target_alt is None:
            self.target_alt = tr.alt_m
        if self.target_heading is None and tr.heading_deg is not None:
            self.target_heading = tr.heading_deg

        alt_err = self.target_alt - tr.alt_m
        pitch = max(-self.max_pitch, min(self.max_pitch, self.kp_alt * alt_err))

        hdg_err = wrap180(self.target_heading - tr.heading_deg) if tr.heading_deg is not None else 0.0
        roll = max(-self.max_roll, min(self.max_roll, self.kp_hdg * hdg_err))

        self.ctx.log("alt_error_m", alt_err)
        self.ctx.log("heading_error_deg", hdg_err)
        return Command(roll_deg=roll, pitch_deg=pitch, yaw_rate_deg_s=0.0, thrust=self.thrust)
