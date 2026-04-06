# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Unreal Engine 5.6 UAV (Unmanned Aerial Vehicle) flight dynamics simulator. Written in C++ with a custom aerodynamics engine, computer vision pipeline (OpenCV), geospatial mapping (Cesium), and integration with external aerodynamic analysis tools (XFoil, OpenVSP).

## Build System

**Build via Visual Studio:**
- Open `UAVSimulator.sln`
- Build target: `UAVSimulator` / `Win64` / `Development Editor`

**Or via Unreal Editor:**
- Right-click `UAVSimulator.uproject` → Generate Visual Studio project files
- Then build from VS or use Unreal's Live Coding (Ctrl+Alt+F11)

**Module dependencies** (defined in `Source/UAVSimulator/UAVSimulator.Build.cs`):
- Public: Core, CoreUObject, Engine, InputCore, AirfoilImporter, RenderCore, RHI, OpenCVHelper, OpenCV
- Private: AssetTools, UnrealEd, PythonScriptPlugin
- Code optimization is intentionally disabled (`OptimizeCode = CodeOptimization.Never`)

**Active plugins:** PythonScriptPlugin, EditorScriptingUtilities, RawInput, OpenCV, CesiumForUnreal, custom `AirfoilImporter`

## Architecture

### Two Physics Approaches

**1. FlightDynamicsComponent** (`Source/UAVSimulator/FlightDynamicsComponent.h/cpp`)  
Simplified model. An actor component attached to an airplane pawn that each tick applies engine thrust, wing lift/drag, and tail lift/drag directly to the root rigid body. Properties (CL/CD curves, thrust, areas) are editor-exposed and configured via DataTables.

**2. PhysicalAirplane + AerodynamicSurfaceSC** (the more sophisticated model)  
Hierarchical surface decomposition:
```
PhysicalAirplane (APawn)
└── AerodynamicSurfaceSC[]          # wings, tail surfaces
    └── SubAerodynamicSurfaceSC[]   # span-wise segments
        └── ControlSurfaceSC        # ailerons, elevators, rudders
```
Each sub-surface reads its airfoil polar from a DataTable, computes angle of attack from the local airflow vector, looks up CL/CD, and applies force+torque to the aircraft's rigid body. Control surfaces (`EFlapType`: Aileron, Elevator, Rudder) can be deflected by pilot input.

### Force Calculation Pattern
`AerodynamicForce` struct holds `PositionalForce` (lift + drag) and `RotationalForce` (torque + r × F). Dynamic pressure = `0.5 * ρ * V²`. Forces are in world space after transforming from surface-local coordinates.

### Aerodynamic Data Pipeline
1. Design aircraft geometry in **OpenVSP** (`Tools/OpenVSP/`)
2. Generate airfoil polars with **XFoil** (`Tools/XFoil/`)
3. Extrapolate polars to 360° AoA using **`Tools/Airfoil/airfoil.py`** (Viterna method via `airfoilprep`)
4. Import `.dat` files into Unreal via the custom **AirfoilImporter** plugin → becomes a `DataTable` of `FAerodynamicProfileRow` (CL/CD/Cm curves keyed by flap angle)

### Computer Vision
`PhysicalAirplane` hosts a `USceneCaptureComponent2D` (640×480, B8G8R8A8). Each frame the render target is read, passed to OpenCV (`OpenCVHelper` plugin) for flip and text overlay, then written to a dynamic texture.

### Key Files
| File | Role |
|------|------|
| `Actor/PhysicalAirplane.h/cpp` | Main aircraft pawn; control inputs, camera/CV, physics kick-off |
| `FlightDynamicsComponent.h/cpp` | Simple tick-based physics component |
| `SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h/cpp` | Per-surface lift/drag computation |
| `SceneComponent/SubAerodynamicSurface/SubAerodynamicSurfaceSC.h/cpp` | Per-segment forces, reads DataTable |
| `SceneComponent/ControlSurface/ControlSurfaceSC.h/cpp` | Deflectable control surfaces |
| `Util/AerodynamicPhysicalCalculationUtil.h/cpp` | Integration with XFoil/OpenVSP tools |
| `Util/AerodynamicUtil.h/cpp` | Chord/airfoil coordinate utilities |
| `DataAsset/` | Unreal DataAsset wrappers for aerodynamic profiles |
| `Entity/` | Plain C++ structs (POD): `AerodynamicForce`, `Chord`, `PolarRow`, etc. |
| `Plugins/AirfoilImporter/` | Custom editor plugin: `.dat` → DataTable factory |

## Physics Configuration

`Config/DefaultEngine.ini` enables physics substepping:
- Max substeps: 16
- Max substep delta time: 0.0013 s (≈769 Hz)

Simulation speed can be scaled at runtime via `DebugSimulatorSpeed` on both `PhysicalAirplane` and `FlightDynamicsComponent` (uses Unreal time dilation).

## Debug & Development

Both physics components expose:
- `DebugConsoleLogs` — prints force values to output log
- `bDrawDebugMarkers` — draws force vectors in viewport

No automated test framework is set up. Testing is done by running the editor and observing simulation behavior with debug markers enabled.

## Notes

- UI-facing property names and some comments are in **Ukrainian** (the development team's language).
- `Tools/Airfoil/airfoil.py` requires `airfoilprep` Python package; `Tools/PyInstall/` and `Tools/OpenVSP/install_to_py.bat` handle dependency setup.
- The large file `Saved/Cesium/...` (SQLite cache ~305 MB) is geospatial tile cache — do not commit.
