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


def split_and_deflect(
    coords: Coords, hinge_x: float, flap_angle_deg: float, gap: float = 0.015, n_arc: int = 24
) -> Tuple[Coords, Coords]:
    """
    Розділяє профіль на крило та щілинний закрилок (slotted flap).
    Гарантує відсутність колізій при будь-якому куті відхилення за рахунок
    динамічного розрахунку безпечної відстані для основного крила.
    """
    if hinge_x <= 0.0:
        return list(coords), []

    arr = np.array(coords)
    le_idx = int(np.argmin(arr[:, 0]))
    upper = coords[: le_idx + 1]  # TE→LE (x зменшується)
    lower = coords[le_idx:]       # LE→TE (x збільшується)

    # ==========================================
    # 1. Формування та обертання закрилки
    # ==========================================
    # Закрилок завжди формуємо починаючи з осі шарніра + половина базового зазору
    x_flap = hinge_x + gap / 2.0
    pt_flap_upper = _interp_at(upper, x_flap)
    pt_flap_lower = _interp_at(lower, x_flap)

    if not pt_flap_upper or not pt_flap_lower:
        raise ValueError(f"Не вдалося інтерполювати точки для закрилки на x={x_flap}")

    y_flap_upper, y_flap_lower = pt_flap_upper[1], pt_flap_lower[1]
    cy_flap = (y_flap_upper + y_flap_lower) / 2.0
    r_flap = (y_flap_upper - y_flap_lower) / 2.0

    upper_flap_fwd = list(reversed([p for p in upper if p[0] > x_flap] + [pt_flap_upper]))
    lower_flap_rev = list(reversed([pt_flap_lower] + [p for p in lower if p[0] > x_flap]))
    
    # Ліве півколо для "носа" закрилки
    le_arc = _arc(x_flap, cy_flap, r_flap, -np.pi / 2, -3 * np.pi / 2, n_arc)
    flap = upper_flap_fwd + lower_flap_rev[1:] + le_arc[1:]

    # Знаходимо центр шарніра (на лінії hinge_x)
    near = arr[np.abs(arr[:, 0] - hinge_x) < 0.05]
    hinge_y = float(np.mean(near[:, 1])) if len(near) > 0 else 0.0

    angle = np.radians(flap_angle_deg)
    cos_a, sin_a = np.cos(angle), np.sin(angle)
    
    rotated_flap = [
        [
            hinge_x + (x - hinge_x) * cos_a - (y - hinge_y) * sin_a,
            hinge_y + (x - hinge_x) * sin_a + (y - hinge_y) * cos_a,
            z,
        ]
        for x, y, z in flap
    ]

    # ==========================================
    # 2. Динамічний розрахунок зрізу для крила
    # ==========================================
    # Шукаємо найлівішу координату X повернутої закрилки
    min_flap_x = min(p[0] for p in rotated_flap)
    
    # Гарантована найправіша межа для крила (жорстко зберігаємо gap)
    safe_max_x = min(min_flap_x - gap, hinge_x - gap / 2.0)

    # Оцінюємо товщину профілю в точці safe_max_x, щоб правильно змістити центр дуги крила
    pt_safe_up = _interp_at(upper, safe_max_x)
    pt_safe_dn = _interp_at(lower, safe_max_x)
    
    if pt_safe_up and pt_safe_dn:
        t_safe = pt_safe_up[1] - pt_safe_dn[1]
        x_wing = safe_max_x - t_safe / 2.0
    else:
        # Резервний варіант
        x_wing = safe_max_x - gap

    # ==========================================
    # 3. Формування основного крила
    # ==========================================
    pt_wing_upper = _interp_at(upper, x_wing)
    pt_wing_lower = _interp_at(lower, x_wing)

    if not pt_wing_upper or not pt_wing_lower:
        raise ValueError(f"Не вдалося інтерполювати точки для крила на x={x_wing}")

    y_wing_upper, y_wing_lower = pt_wing_upper[1], pt_wing_lower[1]
    cy_wing = (y_wing_upper + y_wing_lower) / 2.0
    r_wing = (y_wing_upper - y_wing_lower) / 2.0

    upper_wing = [pt_wing_upper] + [p for p in upper if p[0] < x_wing]
    lower_wing = [p for p in lower if p[0] < x_wing] + [pt_wing_lower]
    
    # Праве півколо для задньої кромки крила
    te_arc = _arc(x_wing, cy_wing, r_wing, -np.pi / 2, np.pi / 2, n_arc)
    wing = upper_wing + lower_wing[1:] + te_arc[1:]

    return wing, rotated_flap

def _interp_at(surface: Coords, x_target: float) -> List[float] | None:
    """Linearly interpolate a point at x_target along a surface segment."""
    for i in range(len(surface) - 1):
        x0, y0, _ = surface[i]
        x1, y1, _ = surface[i + 1]
        if x0 == x1:
            continue
        if min(x0, x1) <= x_target <= max(x0, x1):
            t = (x_target - x0) / (x1 - x0)
            return [x_target, y0 + t * (y1 - y0), 0.0]
    return None

def _arc(
    cx: float, cy: float, r: float, theta_start: float, theta_end: float, n: int = 24
) -> Coords:
    """n evenly-spaced points along an arc centred at (cx, cy) with radius r."""
    angles = np.linspace(theta_start, theta_end, n)
    return [[cx + r * np.cos(a), cy + r * np.sin(a), 0.0] for a in angles]

def generate_mesh(wing_coords: Coords, flap_coords: Coords, is_show: bool = False) -> None:
    has_flap = bool(flap_coords)
    log("INFO", f"Generating {'multi-element' if has_flap else 'single-element'} mesh")
    gmsh.initialize()
    gmsh.option.setNumber("General.Terminal", 1)
    gmsh.model.add("airfoil")

    # Змінено назву функції для логічності
    def _add_closed_curve(coords: Coords) -> Tuple[int, int]:
        pts = [gmsh.model.geo.addPoint(c[0], c[1], 0, meshSize=0.002) for c in coords]
        pts.append(pts[0])  # Замкнути контур
        
        # ВИПРАВЛЕННЯ: Використовуємо Polyline замість Spline. 
        # Це повністю усуває проблему перетину кривих (overshoot).
        curve = gmsh.model.geo.addPolyline(pts)
        
        return curve, gmsh.model.geo.addCurveLoop([curve])

    wing_curve, wing_loop = _add_closed_curve(wing_coords)

    bl_curves = [wing_curve]
    cutouts   = [wing_loop]

    if has_flap:
        flap_curve, flap_loop = _add_closed_curve(flap_coords)
        bl_curves.append(flap_curve)
        cutouts.append(flap_loop)

    # Налаштування примежового шару (Boundary Layer)
    gmsh.model.mesh.field.add("BoundaryLayer", 1)
    gmsh.model.mesh.field.setNumbers(1, "CurvesList", bl_curves)
    gmsh.model.mesh.field.setNumber(1, "Size", 0.00002)
    gmsh.model.mesh.field.setNumber(1, "Ratio", 1.15)
    
    # Товщину залишаємо зменшеною, щоб нормалі не стикалися всередині зазору
    gmsh.model.mesh.field.setNumber(1, "Thickness", 0.0025)
    
    gmsh.model.mesh.field.setNumber(1, "Quads", 1)
    gmsh.model.mesh.field.setAsBoundaryLayer(1)

    # Побудова розрахункового домену (farfield)
    center = gmsh.model.geo.addPoint(0.5,  0, 0, meshSize=2.0)
    far1   = gmsh.model.geo.addPoint(-20,  0, 0, meshSize=4.0)
    far2   = gmsh.model.geo.addPoint( 20,  0, 0, meshSize=4.0)
    arc1 = gmsh.model.geo.addCircleArc(far1, center, far2)
    arc2 = gmsh.model.geo.addCircleArc(far2, center, far1)
    farfield_loop = gmsh.model.geo.addCurveLoop([arc1, arc2])

    surf = gmsh.model.geo.addPlaneSurface([farfield_loop] + cutouts)
    gmsh.model.geo.synchronize()

    # Фізичні групи для SU2
    if has_flap:
        gmsh.model.addPhysicalGroup(1, [wing_curve], name="wing")
        gmsh.model.addPhysicalGroup(1, [flap_curve], name="flap")
    else:
        gmsh.model.addPhysicalGroup(1, [wing_curve], name="airfoil")

    gmsh.model.addPhysicalGroup(1, [arc1, arc2], name="farfield")
    gmsh.model.addPhysicalGroup(2, [surf],       name="fluid")

    gmsh.model.mesh.generate(2)
    gmsh.write(_MESH_FILE) # Переконайтесь, що змінна _MESH_FILE визначена
    
    if is_show:
        gmsh.fltk.run()
    gmsh.finalize()