"""
Виконавець маневрів для UAttitudeControlComponent — «цікавіше за коло».

Той самий канал, що й у circle_autopilot.py:
  • команди SET_ATTITUDE_TARGET по ZMQ PUSH -> tcp://127.0.0.1:5556
  • телеметрія з сенсорної шини ZMQ SUB <- tcp://127.0.0.1:5555 (топік "drone_position")
  • CSV-лог у logs/maneuvers_<маневр>_YYYYmmdd_HHMMSS.csv (команда + телеметрія в рядку)

Керма приймає лише АБСОЛЮТНІ кути (roll/pitch у світовій СК), швидкість рискання
і тягу — тож усі маневри будуються з утримання/розкачки крену та тангажа. Тангаж
там, де не заданий явно, тримає висоту одноконтурним PID (AltitudeHold із circle_autopilot).

Маневри (--maneuver):
  s_turns        — серпантин: почергові крени ±bank кожні --seg-sec, --count сегментів
  figure_eight   — вісімка: віраж праворуч на --eight-deg, потім ліворуч на стільки ж, --loops раз
  wing_rock      — синусоїдна розкачка крену: roll = --amp·sin(2π t/--period), --duration с
  climb_turn     — набір висоти у віражі: крен --bank + тангаж +--pitch-up, поки не набрано --dalt
  spiral_descent — спіраль униз: крутий крен --bank + тангаж −--pitch-down, поки не втрачено --dalt
  roll_doublet   — дублет по крену для ідентифікації: рівно → +--amp → −--amp → рівно
  pitch_doublet  — те саме по тангажу
  sequence       — готова демо-послідовність з кількох маневрів підряд

Перед запуском (як і для circle_autopilot.py):
  • режим симуляції  Playback and Auto Track
  • Sensors mode = Drone, увімкнений сенсор Position
  • DebugSimulatorSpeed = 1.0 на FlightDynamics (інакше час «пливе»)
  • RollPid/PitchPid бажано з gain-scheduling кривою (інакше хитання на швидкості)

Запуск:
  python maneuvers.py --maneuver sequence
  python maneuvers.py --maneuver figure_eight --bank 30 --loops 2
  python maneuvers.py --maneuver wing_rock --amp 35 --period 2.5 --duration 20
  python maneuvers.py --list
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import signal
import sys
import threading
import time
from collections import namedtuple

import zmq

# Консоль Windows часто cp1252 — примусово UTF-8, щоб кирилиця в статусі не валила скрипт.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from circle_autopilot import (  # noqa: E402  (шлях додається вище)
    CMD_ENDPOINT_DEFAULT,
    TLM_ENDPOINT_DEFAULT,
    POSITION_TOPIC,
    Telemetry,
    AltitudeHold,
    receiver_loop,
    build_command,
    _wrap180,
)

# Команда одного тіку. pitch=None -> тангаж рахує AltitudeHold; інакше абсолютні градуси.
Cmd = namedtuple("Cmd", "roll pitch yaw_rate thrust label")


class Ctx:
    """Спільний стан, який читають генератори-маневри. Оновлюється раннером щотіку."""

    def __init__(self, args):
        self.args = args
        self.thrust = args.thrust
        self.level_pitch = args.pitch_deg
        self.althold = AltitudeHold(kp=args.kp_alt, kd=args.kd_alt, ki=args.ki_alt)
        self.target_alt = 0.0
        # оновлюється щотіку
        self.dt = 0.0
        self.t = 0.0
        self.snap = {"have_fix": False, "x": 0.0, "y": 0.0, "z": 0.0,
                     "ground_speed": 0.0, "vertical_speed": 0.0, "track_deg": 0.0,
                     "radius_inst": 0.0, "bus_msgs": 0, "pos_msgs": 0}
        # безперервний (розгорнутий) шляховий курс
        self.heading = 0.0
        self._prev_track = None

    def update_heading(self):
        s = self.snap
        if s["have_fix"] and s["ground_speed"] > 3.0:
            if self._prev_track is not None:
                self.heading += _wrap180(s["track_deg"] - self._prev_track)
            self._prev_track = s["track_deg"]


# ─────────────────────────────────────────────────────────────────────────────
# Маневри — генератори. Кожен yield-ить Cmd для ПОТОЧНОГО тіку; return = маневр завершено.
# Раннер оновлює ctx (dt/t/snap/heading), потім робить gen.send(None).
# ─────────────────────────────────────────────────────────────────────────────

def m_s_turns(ctx, bank, count, seg_sec):
    for i in range(count):
        sign = 1.0 if i % 2 == 0 else -1.0
        t_end = ctx.t + seg_sec
        while ctx.t < t_end:
            yield Cmd(sign * bank, None, 0.0, ctx.thrust,
                      f"S-поворот {i + 1}/{count} ({'→' if sign > 0 else '←'})")


def m_figure_eight(ctx, bank, sweep_deg, loops):
    for i in range(loops):
        for sign, arrow in ((+1.0, "→"), (-1.0, "←")):
            h0 = ctx.heading
            # чекаємо, доки |зміна курсу| не досягне sweep_deg
            while abs(ctx.heading - h0) < sweep_deg - 5.0:
                done = abs(ctx.heading - h0)
                yield Cmd(sign * bank, None, 0.0, ctx.thrust,
                          f"вісімка {i + 1}/{loops} {arrow} {done:.0f}/{sweep_deg:.0f}°")


def m_wing_rock(ctx, amp, period, duration):
    t0 = ctx.t
    while ctx.t - t0 < duration:
        phase = 2.0 * math.pi * (ctx.t - t0) / max(period, 0.1)
        yield Cmd(amp * math.sin(phase), None, 0.0, ctx.thrust,
                  f"розкачка крену {ctx.t - t0:.1f}/{duration:.0f}с")


def m_climb_turn(ctx, bank, pitch_up, dalt):
    z0 = None
    while True:
        if z0 is None and ctx.snap["have_fix"]:
            z0 = ctx.snap["z"]
        gained = (ctx.snap["z"] - z0) if z0 is not None else 0.0
        if z0 is not None and gained >= dalt:
            return
        yield Cmd(bank, pitch_up, 0.0, 1.0,
                  f"набір у віражі +{gained:.0f}/{dalt:.0f} м")


def m_spiral_descent(ctx, bank, pitch_down, dalt):
    z0 = None
    while True:
        if z0 is None and ctx.snap["have_fix"]:
            z0 = ctx.snap["z"]
        lost = (z0 - ctx.snap["z"]) if z0 is not None else 0.0
        if z0 is not None and lost >= dalt:
            return
        yield Cmd(bank, -abs(pitch_down), 0.0, 0.35,
                  f"спіраль вниз −{lost:.0f}/{dalt:.0f} м")


def _doublet(ctx, axis, amp, hold_sec):
    """axis: 'roll' або 'pitch'. Рівно → +amp → −amp → рівно."""
    seq = ((0.0, 2.0), (+amp, hold_sec), (-amp, hold_sec), (0.0, 3.0))
    for value, dur in seq:
        t_end = ctx.t + dur
        while ctx.t < t_end:
            if axis == "roll":
                yield Cmd(value, 0.0, 0.0, ctx.thrust, f"roll-дублет {value:+.0f}°")
            else:
                yield Cmd(0.0, value, 0.0, ctx.thrust, f"pitch-дублет {value:+.0f}°")


def m_roll_doublet(ctx, amp, hold_sec):
    yield from _doublet(ctx, "roll", amp, hold_sec)


def m_pitch_doublet(ctx, amp, hold_sec):
    yield from _doublet(ctx, "pitch", amp, hold_sec)


def m_sequence(ctx):
    """Готова демонстрація: серпантин → вісімка → набір у віражі → розкачка → спіраль вниз."""
    yield from m_s_turns(ctx, bank=20.0, count=4, seg_sec=5.0)
    yield from m_figure_eight(ctx, bank=30.0, sweep_deg=360.0, loops=1)
    yield from m_climb_turn(ctx, bank=25.0, pitch_up=8.0, dalt=60.0)
    yield from m_wing_rock(ctx, amp=25.0, period=2.5, duration=15.0)
    yield from m_spiral_descent(ctx, bank=35.0, pitch_down=6.0, dalt=60.0)


def build_maneuver(name, ctx, args):
    a = args
    table = {
        "s_turns":        lambda: m_s_turns(ctx, a.bank, a.count, a.seg_sec),
        "figure_eight":   lambda: m_figure_eight(ctx, a.bank, a.eight_deg, a.loops),
        "wing_rock":      lambda: m_wing_rock(ctx, a.amp, a.period, a.duration),
        "climb_turn":     lambda: m_climb_turn(ctx, a.bank, a.pitch_up, a.dalt),
        "spiral_descent": lambda: m_spiral_descent(ctx, a.bank, a.pitch_down, a.dalt),
        "roll_doublet":   lambda: m_roll_doublet(ctx, a.amp, a.hold_sec),
        "pitch_doublet":  lambda: m_pitch_doublet(ctx, a.amp, a.hold_sec),
        "sequence":       lambda: m_sequence(ctx),
    }
    if name not in table:
        raise SystemExit(f"невідомий маневр '{name}'. Доступні: {', '.join(table)}")
    return table[name]()


MANEUVERS = ["s_turns", "figure_eight", "wing_rock", "climb_turn",
             "spiral_descent", "roll_doublet", "pitch_doublet", "sequence"]


# ─────────────────────────────────────────────────────────────────────────────
# Раннер
# ─────────────────────────────────────────────────────────────────────────────

def run(args):
    if args.list:
        print("Маневри:", ", ".join(MANEUVERS))
        return

    os.makedirs(args.out, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    csv_path = os.path.join(args.out, f"maneuvers_{args.maneuver}_{stamp}.csv")

    ctx = zmq.Context.instance()
    push = ctx.socket(zmq.PUSH)
    push.setsockopt(zmq.SNDHWM, 1)
    push.setsockopt(zmq.LINGER, 0)
    push.connect(args.cmd_endpoint)

    tlm = Telemetry()
    stop = threading.Event()
    rx = threading.Thread(target=receiver_loop, args=(args.tlm_endpoint, tlm, stop), daemon=True)
    rx.start()
    signal.signal(signal.SIGINT, lambda *_: stop.set())

    print(__doc__.split("Запуск:")[0])
    print(f"Маневр: {args.maneuver}")
    print(f"Команди  PUSH -> {args.cmd_endpoint}")
    print(f"Телеметрія SUB <- {args.tlm_endpoint}  (топік '{POSITION_TOPIC}')")
    print(f"Лог: {csv_path}\n")

    fields = ["t_s", "maneuver", "label", "roll_cmd_deg", "pitch_cmd_deg", "yaw_rate_cmd_deg_s",
              "thrust_cmd", "have_fix", "x_m", "y_m", "z_m", "alt_err_m", "gs_mps", "vs_mps",
              "track_deg", "heading_unwrapped_deg", "radius_inst_m"]
    csv_file = open(csv_path, "w", newline="", encoding="utf-8")
    writer = csv.DictWriter(csv_file, fieldnames=fields)
    writer.writeheader()

    mctx = Ctx(args)
    dt_target = 1.0 / args.rate
    t0 = time.perf_counter()
    last = t0
    last_status = 0.0

    phase = "SETTLE"
    gen = None
    man_started_t = None
    recover_until = None
    warned_no_tlm = False

    try:
        while not stop.is_set():
            now = time.perf_counter()
            dt = now - last
            last = now
            t = now - t0

            snap = tlm.snapshot()
            mctx.dt, mctx.t, mctx.snap = dt, t, snap
            mctx.update_heading()

            roll_cmd = 0.0
            pitch_src = None      # None -> alt hold; число -> абсолютний тангаж
            yaw_rate_cmd = 0.0
            thrust_cmd = args.thrust
            label = phase

            if phase == "SETTLE":
                pitch_src = 0.0
                if t >= args.settle_sec:
                    mctx.target_alt = (args.target_alt if args.target_alt is not None
                                       else (snap["z"] if snap["have_fix"] else 0.0))
                    gen = build_maneuver(args.maneuver, mctx, args)
                    try:
                        cmd = next(gen)
                    except StopIteration:
                        cmd = None
                    man_started_t = t
                    phase = "MANEUVER"
                    print(f"\n[t={t:5.1f}] -> MANEUVER '{args.maneuver}' (ціль висоти {mctx.target_alt:.0f} м)")
                    if cmd:
                        roll_cmd, pitch_src, yaw_rate_cmd, thrust_cmd, label = cmd

            elif phase == "MANEUVER":
                over_budget = (t - man_started_t) >= args.max_sec
                try:
                    if over_budget:
                        raise StopIteration
                    cmd = gen.send(None)
                    roll_cmd, pitch_src, yaw_rate_cmd, thrust_cmd, label = cmd
                except StopIteration:
                    phase = "RECOVER"
                    recover_until = t + args.recover_sec
                    why = "ліміт часу" if over_budget else "завершено"
                    print(f"\n[t={t:5.1f}] -> RECOVER ({why})")
                    pitch_src = None

            elif phase == "RECOVER":
                roll_cmd = 0.0
                pitch_src = None
                label = "RECOVER"
                if t >= recover_until:
                    break

            # тангаж: явний або з утримання висоти
            if pitch_src is None:
                if snap["have_fix"]:
                    pitch_cmd = mctx.althold.update(mctx.target_alt, snap["z"],
                                                    snap["vertical_speed"], dt)
                else:
                    pitch_cmd = args.pitch_deg
            else:
                pitch_cmd = float(pitch_src)

            try:
                push.send_json(build_command(roll_cmd, pitch_cmd, yaw_rate_cmd, thrust_cmd),
                               flags=zmq.NOBLOCK)
            except zmq.Again:
                pass

            alt_err = (mctx.target_alt - snap["z"]) if snap["have_fix"] else ""
            writer.writerow({
                "t_s": f"{t:.3f}", "maneuver": args.maneuver, "label": label,
                "roll_cmd_deg": f"{roll_cmd:.2f}", "pitch_cmd_deg": f"{pitch_cmd:.2f}",
                "yaw_rate_cmd_deg_s": f"{yaw_rate_cmd:.2f}", "thrust_cmd": f"{thrust_cmd:.3f}",
                "have_fix": int(snap["have_fix"]),
                "x_m": f"{snap['x']:.2f}", "y_m": f"{snap['y']:.2f}", "z_m": f"{snap['z']:.2f}",
                "alt_err_m": (f"{alt_err:.2f}" if alt_err != "" else ""),
                "gs_mps": f"{snap['ground_speed']:.2f}", "vs_mps": f"{snap['vertical_speed']:.2f}",
                "track_deg": f"{snap['track_deg']:.1f}", "heading_unwrapped_deg": f"{mctx.heading:.1f}",
                "radius_inst_m": f"{snap['radius_inst']:.1f}",
            })
            csv_file.flush()

            if t - last_status >= 0.5:
                last_status = t
                if not snap["have_fix"] and t > args.settle_sec + 2.0 and not warned_no_tlm:
                    warned_no_tlm = True
                    print(f"\n!!! Нема '{POSITION_TOPIC}' з шини (bus={snap['bus_msgs']}). "
                          "Увімкни сенсор Position / режим Playback and Auto Track. "
                          "Тангаж поки тримається на constant pitch.\n")
                fix = "OK" if snap["have_fix"] else "--"
                sys.stdout.write(
                    f"\r[{phase:8}] t={t:6.1f}s fix={fix} | {label[:34]:34} | "
                    f"roll={roll_cmd:+6.1f} pitch={pitch_cmd:+5.1f} thr={thrust_cmd:.2f} | "
                    f"z={snap['z']:7.1f} GS={snap['ground_speed']:5.1f} "
                    f"hdg={mctx.heading:+7.1f} R~{snap['radius_inst']:6.0f}".ljust(160)
                )
                sys.stdout.flush()

            slp = dt_target - (time.perf_counter() - now)
            if slp > 0:
                time.sleep(slp)
    finally:
        for _ in range(5):
            try:
                push.send_json(build_command(0.0, 0.0, 0.0, args.thrust), flags=zmq.NOBLOCK)
            except zmq.Again:
                pass
            time.sleep(0.02)
        stop.set()
        rx.join(timeout=1.0)
        csv_file.close()
        push.close(0)
        snap = tlm.snapshot()
        print("\n\n───────── ПІДСУМОК ─────────")
        print(f"маневр: {args.maneuver}")
        print(f"шина: конвертів={snap['bus_msgs']}  drone_position={snap['pos_msgs']}")
        print(f"розгорнутий курс за весь політ: {mctx.heading:+.1f}°")
        print(f"CSV: {csv_path}")


def parse_args():
    p = argparse.ArgumentParser(description="Виконавець маневрів для UAttitudeControlComponent.")
    p.add_argument("--maneuver", default="sequence", choices=MANEUVERS, help="Який маневр виконати.")
    p.add_argument("--list", action="store_true", help="Показати список маневрів і вийти.")

    p.add_argument("--cmd-endpoint", default=CMD_ENDPOINT_DEFAULT)
    p.add_argument("--tlm-endpoint", default=TLM_ENDPOINT_DEFAULT)
    p.add_argument("--rate", type=float, default=20.0, help="Частота команд/логу, Гц.")
    p.add_argument("--thrust", type=float, default=0.65, help="Базова тяга 0..1.")
    p.add_argument("--settle-sec", type=float, default=5.0)
    p.add_argument("--recover-sec", type=float, default=5.0)
    p.add_argument("--max-sec", type=float, default=240.0, help="Захисний ліміт тривалості маневру.")
    p.add_argument("--target-alt", type=float, default=None, help="Цільова висота, м (за замовч. — поточна на старті).")
    p.add_argument("--pitch-deg", type=float, default=2.0, help="Тангаж, коли нема телеметрії для утримання висоти.")

    # спільні ручки маневрів
    p.add_argument("--bank", type=float, default=25.0, help="Кут крену у віражних маневрах, град.")
    p.add_argument("--count", type=int, default=6, help="s_turns: кількість сегментів.")
    p.add_argument("--seg-sec", type=float, default=5.0, help="s_turns: тривалість сегмента, с.")
    p.add_argument("--eight-deg", type=float, default=360.0, help="figure_eight: скільки градусів курсу на пів-вісімку.")
    p.add_argument("--loops", type=int, default=1, help="figure_eight: скільки вісімок.")
    p.add_argument("--amp", type=float, default=30.0, help="wing_rock/дублети: амплітуда, град.")
    p.add_argument("--period", type=float, default=3.0, help="wing_rock: період синусоїди, с.")
    p.add_argument("--duration", type=float, default=20.0, help="wing_rock: тривалість, с.")
    p.add_argument("--hold-sec", type=float, default=1.5, help="Дублети: тривалість кожної полиці, с.")
    p.add_argument("--pitch-up", type=float, default=8.0, help="climb_turn: тангаж кабрування, град.")
    p.add_argument("--pitch-down", type=float, default=6.0, help="spiral_descent: тангаж пікірування, град.")
    p.add_argument("--dalt", type=float, default=60.0, help="climb_turn/spiral_descent: зміна висоти, м.")

    p.add_argument("--kp-alt", type=float, default=0.30)
    p.add_argument("--kd-alt", type=float, default=1.50)
    p.add_argument("--ki-alt", type=float, default=0.03)
    p.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs"))
    return p.parse_args()


if __name__ == "__main__":
    run(parse_args())
