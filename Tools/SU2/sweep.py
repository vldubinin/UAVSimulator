import json
import numpy as np
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional, Set, Tuple

from airfoilprep import Airfoil, Polar as AirfoilPolar
from logger import log
from geometry import split_and_deflect, generate_mesh
from su2_runner import run_su2, read_su2_results

_BASE_DIR = Path(__file__).parent

Polar     = Dict[float, Dict[float, Optional[Dict]]]
Completed = Set[Tuple[float, float]]

_META_KEY           = "__meta__"
_STATUS_IN_PROGRESS = "InProgress"
_STATUS_COMPLETED   = "Completed"

# Cache: path string -> byte offset where the metadata line starts
_meta_offsets: Dict[str, int] = {}


@dataclass
class SweepConfig:
    profile_path:   str
    surface_name:   str
    core_number:    int
    hinge_location: float
    min_flap:       float
    max_flap:       float
    flap_step:      float
    rms_quality:    float
    resume:         bool
    re:             float = 750000.0
    cd_max:         float = 1.25


def _name_stem(cfg: SweepConfig) -> str:
    stem = Path(cfg.profile_path).stem
    return f"{stem}_{cfg.hinge_location}_{cfg.min_flap}_{cfg.max_flap}"


def history_path(cfg: SweepConfig) -> Path:
    return _BASE_DIR / f"history_{_name_stem(cfg)}.data"


def _flap_sweep_order(cfg: SweepConfig) -> list[float]:
    if cfg.min_flap == cfg.max_flap:
        return [cfg.min_flap]

    # Якщо діапазон повністю від'ємний (наприклад, [-45, -40])
    if cfg.max_flap <= 0:
        # Йдемо від max_flap (ближче до 0) вниз до min_flap
        return np.arange(cfg.max_flap, cfg.min_flap - cfg.flap_step * 0.5, -cfg.flap_step).tolist()
    
    # Якщо діапазон повністю додатний (наприклад, [40, 45])
    if cfg.min_flap >= 0:
        # Йдемо від min_flap (ближче до 0) вгору до max_flap
        return np.arange(cfg.min_flap, cfg.max_flap + cfg.flap_step * 0.5, cfg.flap_step).tolist()
    
    # Якщо діапазон перетинає нуль (наприклад, [-10, 10])
    # Класична логіка: від 0 вгору, потім від -step вниз
    pos_sweep = np.arange(0, cfg.max_flap + cfg.flap_step * 0.5, cfg.flap_step).tolist()
    neg_sweep = np.arange(-cfg.flap_step, cfg.min_flap - cfg.flap_step * 0.5, -cfg.flap_step).tolist()
    
    return pos_sweep + neg_sweep


def _find_meta_offset(path: Path) -> int:
    """Return the byte offset where the metadata line starts, or file size if none."""
    if not path.exists():
        return 0
    with path.open("rb") as f:
        f.seek(0, 2)
        size = f.tell()
        if size == 0:
            return 0
        chunk_size = min(size, 8192)
        f.seek(size - chunk_size)
        chunk = f.read()

    text  = chunk.decode("utf-8", errors="replace")
    lines = text.splitlines(keepends=True)

    pos = size
    for line in reversed(lines):
        pos -= len(line.encode("utf-8"))
        stripped = line.strip()
        if not stripped:
            continue
        try:
            if _META_KEY in json.loads(stripped):
                return pos
        except json.JSONDecodeError:
            pass
        return size  # last non-empty line is a data entry, not metadata
    return 0


def _get_meta_offset(path: Path) -> int:
    key = str(path)
    if key not in _meta_offsets:
        _meta_offsets[key] = _find_meta_offset(path)
    return _meta_offsets[key]


def load_history(path: Path) -> Tuple[Polar, Optional[Dict]]:
    """
    Load polar data and metadata from a history file.

    Returns (polar, meta) where:
      - polar: flap -> {aoa -> {CL, CD, CMz}} built from data entries
      - meta:  {"last_flap": float, "last_aoa": int, "status": str} or None if absent
    """
    polar: Polar = {}
    meta: Optional[Dict] = None

    if not path.exists():
        return polar, meta

    with path.open(encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if not stripped:
                continue
            try:
                entry = json.loads(stripped)
                if _META_KEY in entry:
                    meta = entry[_META_KEY]
                    continue
                flap = entry["condition"]["flap"]
                aoa  = entry["condition"]["AoA"]
                res  = entry["result"]
                polar.setdefault(flap, {})[aoa] = {"CL": res["CL"], "CD": res["CD"], "CMz": res["CMz"]}
            except (KeyError, json.JSONDecodeError) as e:
                log("WARNING", f"Skipping malformed history entry: {e}")

    # Prime the offset cache so subsequent writes know where the meta line is
    _meta_offsets[str(path)] = _find_meta_offset(path)
    return polar, meta


def build_completed_from_meta(cfg: SweepConfig, last_flap, last_aoa) -> Completed:
    """
    Reconstruct the completed set from the sweep order up to and including
    the last recorded (flap, aoa) position stored in the history metadata.
    """
    flap_angles = _flap_sweep_order(cfg)
    aoa_order   = list(range(0, 21, 1)) + list(range(-1, -16, -1))

    completed: Completed = set()
    for flap in flap_angles:
        for aoa in aoa_order:
            completed.add((flap, aoa))
            if abs(flap - float(last_flap)) < 1e-9 and aoa == int(last_aoa):
                return completed
    return completed


def _append_history(path: Path, flap: float, aoa: float, result: Dict) -> None:
    """Append a result entry and update the metadata line (always kept as last line)."""
    data_line = json.dumps({
        "condition": {"flap": flap, "AoA": aoa},
        "result":    {"CL": result["CL"], "CD": result["CD"], "CMz": result["CMz"]},
    }) + "\n"
    meta_line = json.dumps({
        _META_KEY: {"last_flap": flap, "last_aoa": aoa, "status": _STATUS_IN_PROGRESS}
    }) + "\n"

    data_bytes = data_line.encode("utf-8")
    meta_bytes = meta_line.encode("utf-8")
    offset     = _get_meta_offset(path)

    path.touch(exist_ok=True)
    with path.open("r+b") as f:
        f.seek(offset)
        f.truncate()
        f.write(data_bytes)
        f.write(meta_bytes)

    _meta_offsets[str(path)] = offset + len(data_bytes)


def _finalize_history(path: Path) -> None:
    """Update the metadata line to set status=Completed."""
    if not path.exists() or path.stat().st_size == 0:
        return

    offset = _get_meta_offset(path)

    with path.open("rb") as f:
        f.seek(offset)
        raw = f.read()

    meta_text = raw.decode("utf-8").split("\n")[0].strip()
    if not meta_text:
        return

    try:
        entry = json.loads(meta_text)
        if _META_KEY not in entry:
            return
        entry[_META_KEY]["status"] = _STATUS_COMPLETED
        new_meta_line = json.dumps(entry) + "\n"
        with path.open("r+b") as f:
            f.seek(offset)
            f.truncate()
            f.write(new_meta_line.encode("utf-8"))
    except json.JSONDecodeError:
        pass


def _sweep_range(polar: Polar, flap: float, aoa_range, cfg: SweepConfig,
                 hist_path: Path, completed: Completed) -> None:
    prev_in_run = False
    rms_quality = cfg.rms_quality
    if flap > 25.0 or flap < -25.0:
        rms_quality = rms_quality + 1.0
        log("INFO", f"Rms_quality was changed to {rms_quality}")
    
    for aoa in aoa_range:
        if (flap, aoa) in completed:
            log("INFO", f"Skip (done): flap={flap:+.1f}deg AoA={aoa:+.1f}deg")
            prev_in_run = False
            continue

        log("INFO", f"flap={flap:+.1f}deg  AoA={aoa:+.1f}deg")
        
        try:
            run_su2(aoa, cfg.core_number, cfg.hinge_location, rms_quality, use_restart=prev_in_run)
        except Exception:
            polar[flap][aoa] = None
            prev_in_run = False
            log("WARNING", f"SU2 process error — stopping AoA sweep for flap={flap:+.1f}deg")
            break

        r = read_su2_results()

        if r is None:
            polar[flap][aoa] = None
            prev_in_run = False
            log("WARNING", f"Simulation failed: flap={flap:+.1f}deg AoA={aoa:+.1f}deg")
            break

        if r["rms_rho"] > rms_quality:
            log("WARNING", f"rms[Rho]={r['rms_rho']:.3f} exceeds threshold {rms_quality}, stopping AoA sweep")
            break

        polar[flap][aoa] = {"CL": r["CL"], "CD": r["CD"], "CMz": r["CMz"]}
        _append_history(hist_path, flap, aoa, r)
        prev_in_run = True
        log("INFO", f"CL={r['CL']:.5f}  CD={r['CD']:.5f}  CMz={r['CMz']:.5f}  rms[Rho]={r['rms_rho']:.3f}")


def run_polar_sweep(base_coords, cfg: SweepConfig, polar: Polar, completed: Completed) -> Polar:
    hist_path   = history_path(cfg)
    flap_angles = _flap_sweep_order(cfg)

    for flap in flap_angles:
        polar.setdefault(flap, {})
        log("INFO", f"Flap={flap:.1f}deg — generating mesh")
        try:
            wing, flap_coords = split_and_deflect(base_coords, cfg.hinge_location, flap)
            generate_mesh(wing, flap_coords)
        except Exception as e:
            log("ERROR", f"Mesh generation failed for flap={flap:.1f}deg: {e}")
            continue

        try:
            _sweep_range(polar, flap, range(0, 21, 1),    cfg, hist_path, completed)
            _sweep_range(polar, flap, range(-1, -16, -1), cfg, hist_path, completed)
        except Exception as e:
            log("ERROR", f"Sweep failed for flap={flap:.1f}deg: {e}")
            continue

    _finalize_history(hist_path)
    return polar


def save_polar(polar: Polar, surface_name: str) -> None:
    file_path = _BASE_DIR / f"polar_{surface_name}.json"
    serializable = {
        str(flap): {str(aoa): r for aoa, r in aoa_map.items()}
        for flap, aoa_map in polar.items()
    }
    with file_path.open("w", encoding="utf-8") as f:
        json.dump(serializable, f, indent=2)
    log("INFO", f"Polar saved to: {file_path.name}")


def extrapolate_and_save(polar: Polar, cfg: SweepConfig) -> Path:
    results = []
    for flap, aoa_map in sorted(polar.items()):
        # Collect valid points, sorted ascending by AoA (required by airfoilprep)
        valid = sorted(
            [(aoa, r) for aoa, r in aoa_map.items() if r is not None],
            key=lambda x: x[0]
        )
        if not valid:
            log("WARNING", f"No valid data for flap={flap:.1f}deg, skipping extrapolation")
            continue

        alphas = [v[0] for v in valid]
        cls    = [v[1]["CL"]  for v in valid]
        cds    = [v[1]["CD"]  for v in valid]
        cms    = [v[1]["CMz"] for v in valid]

        airfoil_obj = Airfoil([AirfoilPolar(cfg.re, alphas, cls, cds, cms)])
        ext_polar   = airfoil_obj.extrapolate(cfg.cd_max).getPolar(cfg.re)

        cl_curve = [{"Time": float(a), "Value": float(c)}
                    for a, c in zip(ext_polar.alpha, ext_polar.cl)]
        cd_curve = [{"Time": float(a), "Value": float(c)}
                    for a, c in zip(ext_polar.alpha, ext_polar.cd)]
        cm_curve = [{"Time": float(a), "Value": float(c)}
                    for a, c in zip(ext_polar.alpha, ext_polar.cm)]

        results.append({
            "Name":      f"FLAP_{int(flap)}_Deg",
            "FlapAngle": flap,
            "ClVsAoA":   cl_curve,
            "CdVsAoA":   cd_curve,
            "CmVsAoA":   cm_curve,
        })
        log("INFO", f"Extrapolated flap={flap:.1f}deg — {len(ext_polar.alpha)} AoA points")

    file_path = _BASE_DIR / f"extrapolated_{_name_stem(cfg)}.json"
    with file_path.open("w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)
    log("INFO", f"Extrapolated polar saved to: {file_path.name}")
    return file_path
