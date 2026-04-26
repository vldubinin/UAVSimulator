import matplotlib
matplotlib.use("Agg")  # non-interactive backend — safe without a display server
import matplotlib.pyplot as plt
from pathlib import Path

from logger import log
from geometry import split_and_deflect

_SCRIPT_DIR = Path(__file__).resolve().parent


def _name_stem(cfg) -> str:
    stem = Path(cfg.profile_path).stem
    return f"{stem}_{cfg.hinge_location}_{cfg.min_flap}_{cfg.max_flap}"


def _plot_outline(ax, coords, color, label=None, alpha=0.85, linewidth=1.2):
    xs = [p[0] for p in coords]
    ys = [p[1] for p in coords]
    ax.plot(xs + [xs[0]], ys + [ys[0]], color=color, alpha=alpha,
            linewidth=linewidth, label=label)


def _render(base_coords, cfg) -> None:
    stem = Path(cfg.profile_path).stem
    out_path = _SCRIPT_DIR / "visualisation" / stem / f"{_name_stem(cfg)}.png"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(10, 4))
    ax.set_aspect("equal")
    ax.grid(True, linestyle="--", linewidth=0.5, alpha=0.6)
    ax.set_xlabel("x/c")
    ax.set_ylabel("y/c")

    if cfg.hinge_location <= 0.0:
        wing, _ = split_and_deflect(base_coords, cfg.hinge_location, 0.0)
        _plot_outline(ax, wing, color="C0", label=stem)
        ax.set_title(f"{stem}  |  clean wing (no flap)")
    else:
        angles = list(dict.fromkeys([cfg.min_flap, 0.0, cfg.max_flap]))
        for i, angle in enumerate(angles):
            wing, flap = split_and_deflect(base_coords, cfg.hinge_location, angle)
            color = f"C{i}"
            _plot_outline(ax, wing,  color=color, label=f"{angle:+.1f}°")
            _plot_outline(ax, flap,  color=color, alpha=0.85, linewidth=1.2)

        ax.axvline(cfg.hinge_location, color="black", linestyle="--",
                   linewidth=0.8, alpha=0.5, label=f"hinge x={cfg.hinge_location}")
        ax.set_title(
            f"{stem}  |  hinge={cfg.hinge_location}  "
            f"flap=[{cfg.min_flap}°, {cfg.max_flap}°]"
        )

    ax.legend(loc="upper right")
    fig.savefig(out_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    log("INFO", f"Geometry visualisation saved: {out_path}")


def visualise_geometry(base_coords, cfg) -> None:
    try:
        _render(base_coords, cfg)
    except Exception as e:
        log("ERROR", f"visualise_geometry failed: {e}")
