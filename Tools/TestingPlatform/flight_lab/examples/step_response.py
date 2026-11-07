"""
Діагностичний тест: ступінчасті уставки крену й тангажу за фіксованою програмою.
Перевіряє, що літак узагалі слухається команд, з правильним знаком, і наскільки швидко
та точно внутрішній PID автопілота (UAttitudeControlComponent) відпрацьовує уставки.

    uavlab run examples/step_response.py -c test_configs/step_response.yaml

Програма (тривалості — controller_params.trim_s / segment_s):
    trim (розкрутка двигуна, стабілізація швидкості) -> крен +R -> рівний -> крен -R -> рівний
    -> тангаж +P -> рівний -> тангаж -P -> рівний
Кожен ступінь стартує з рівного польоту, тож t90/перерегулювання рахуються від 0.
"""

from uavlab import Command, Controller


class StepResponse(Controller):
    rate_hz = 20

    def on_start(self, ctx):
        p = ctx.params
        seg = float(p.get("segment_s", 6.0))
        trim = float(p.get("trim_s", 10.0))
        r, up, down = float(p.get("roll_step_deg", 20)), float(p.get("pitch_up_deg", 5)), float(p.get("pitch_down_deg", -3))
        self.thrust = float(p.get("thrust", 0.7))
        # (тривалість, назва фази, крен, тангаж)
        self.program = [
            (trim, "trim", 0.0, 0.0),
            (seg, "roll_right", r, 0.0),
            (seg, "level", 0.0, 0.0),
            (seg, "roll_left", -r, 0.0),
            (seg, "level", 0.0, 0.0),
            (seg, "pitch_up", 0.0, up),
            (seg, "level", 0.0, 0.0),
            (seg, "pitch_down", 0.0, down),
            (seg, "level", 0.0, 0.0),
        ]
        self.t0 = None
        self.phase = None

    def _current(self, t):
        elapsed = 0.0
        for dur, name, roll, pitch in self.program:
            elapsed += dur
            if t < elapsed:
                return name, roll, pitch
        return None

    def step(self, obs):
        if self.t0 is None:
            self.t0 = obs.t
        cur = self._current(obs.t - self.t0)
        if cur is None:
            return Command(thrust=self.thrust)
        name, roll, pitch = cur
        if name != self.phase:
            self.ctx.event("phase", name=name, roll_deg=roll, pitch_deg=pitch)
            self.phase = name
        self.ctx.log("phase", name)
        if obs.truth is not None:
            self.ctx.log("roll_err_deg", roll - obs.truth.roll_deg)
            self.ctx.log("pitch_err_deg", pitch - obs.truth.pitch_deg)
        return Command(roll_deg=roll, pitch_deg=pitch, yaw_rate_deg_s=0.0, thrust=self.thrust)

    def is_done(self, obs):
        return self.t0 is not None and self._current(obs.t - self.t0) is None
