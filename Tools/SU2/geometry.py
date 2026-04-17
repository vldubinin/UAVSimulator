import gmsh
import numpy as np
from pathlib import Path
from typing import List, Tuple

from logger import log

_MESH_FILE = str(Path(__file__).parent / "airfoil.su2")

Coords = List[List[float]]


def load_airfoil_dat(file_path: str) -> Coords:
    coords = []
    with open(file_path) as f:
        for line in f.readlines()[1:]:
            parts = line.split()
            if len(parts) >= 2:
                coords.append([float(parts[0]), float(parts[1]), 0.0])
    return coords


def split_and_deflect(coords: Coords, hinge_x: float, flap_angle_deg: float, gap: float = 0.015) -> Tuple[Coords, Coords]:
    # hinge_x <= 0 means no flap — return the full airfoil as the wing
    if hinge_x <= 0.0:
        return list(coords), []

    arr = np.array(coords)
    near = arr[np.abs(arr[:, 0] - hinge_x) < 0.05]
    hinge_y = float(np.mean(near[:, 1])) if len(near) > 0 else 0.0

    wing, flap_upper, flap_lower = [], [], []
    phase = 0
    for x, y, z in coords:
        if phase == 0:
            if x > hinge_x + gap / 2:
                flap_upper.append([x, y, z])
            else:
                phase = 1
        if phase == 1:
            if x <= hinge_x - gap / 2:
                wing.append([x, y, z])
            elif x > hinge_x + gap / 2:
                phase = 2
        if phase == 2:
            if x > hinge_x + gap / 2:
                flap_lower.append([x, y, z])

    flap = flap_upper + flap_lower
    if not wing or not flap:
        raise ValueError(f"Geometry split failed at hinge_x={hinge_x}. Check coordinates or reduce gap.")

    angle = np.radians(flap_angle_deg)
    cos_a, sin_a = np.cos(angle), np.sin(angle)
    rotated_flap = [
        [hinge_x + (x - hinge_x) * cos_a - (y - hinge_y) * sin_a,
         hinge_y + (x - hinge_x) * sin_a + (y - hinge_y) * cos_a, z]
        for x, y, z in flap
    ]
    return wing, rotated_flap


def generate_mesh(wing_coords: Coords, flap_coords: Coords) -> None:
    has_flap = bool(flap_coords)
    log("INFO", f"Generating {'multi-element' if has_flap else 'single-element'} mesh")
    gmsh.initialize()
    gmsh.option.setNumber("General.Terminal", 1)
    gmsh.model.add("airfoil")

    def _add_closed_spline(coords: Coords) -> Tuple[int, int]:
        pts = [gmsh.model.geo.addPoint(c[0], c[1], 0, meshSize=0.002) for c in coords]
        pts.append(pts[0])
        curve = gmsh.model.geo.addSpline(pts)
        return curve, gmsh.model.geo.addCurveLoop([curve])

    wing_curve, wing_loop = _add_closed_spline(wing_coords)

    bl_curves = [wing_curve]
    cutouts   = [wing_loop]

    if has_flap:
        flap_curve, flap_loop = _add_closed_spline(flap_coords)
        bl_curves.append(flap_curve)
        cutouts.append(flap_loop)

    gmsh.model.mesh.field.add("BoundaryLayer", 1)
    gmsh.model.mesh.field.setNumbers(1, "CurvesList", bl_curves)
    gmsh.model.mesh.field.setNumber(1, "Size", 0.00002)
    gmsh.model.mesh.field.setNumber(1, "Ratio", 1.15)
    gmsh.model.mesh.field.setNumber(1, "Thickness", 0.015)
    gmsh.model.mesh.field.setNumber(1, "Quads", 1)
    gmsh.model.mesh.field.setAsBoundaryLayer(1)

    center = gmsh.model.geo.addPoint(0.5,  0, 0, meshSize=2.0)
    far1   = gmsh.model.geo.addPoint(-20,  0, 0, meshSize=4.0)
    far2   = gmsh.model.geo.addPoint( 20,  0, 0, meshSize=4.0)
    arc1 = gmsh.model.geo.addCircleArc(far1, center, far2)
    arc2 = gmsh.model.geo.addCircleArc(far2, center, far1)
    farfield_loop = gmsh.model.geo.addCurveLoop([arc1, arc2])

    surf = gmsh.model.geo.addPlaneSurface([farfield_loop] + cutouts)
    gmsh.model.geo.synchronize()

    gmsh.model.addPhysicalGroup(1, bl_curves,        name="airfoil")
    gmsh.model.addPhysicalGroup(1, [arc1, arc2],     name="farfield")
    gmsh.model.addPhysicalGroup(2, [surf],            name="fluid")

    gmsh.model.mesh.generate(2)
    gmsh.write(_MESH_FILE)
    gmsh.finalize()
