# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Unreal Engine 5.7 UAV (Unmanned Aerial Vehicle) flight dynamics simulator. Written in C++ with a custom aerodynamics engine, computer vision pipeline (OpenCV), geospatial mapping (Cesium), and integration with external aerodynamic analysis tools (XFoil, SU2, OpenVSP).

## Build System

**Build via Visual Studio:**
- Open `UAVSimulator.sln`
- Build target: `UAVSimulator` / `Win64` / `Development Editor`

**Or via Unreal Editor:**
- Right-click `UAVSimulator.uproject` → Generate Visual Studio project files
- Then build from VS or use Unreal's Live Coding (Ctrl+Alt+F11)

**Module dependencies** (defined in `Source/UAVSimulator/UAVSimulator.Build.cs`):
- Public: Core, CoreUObject, Engine, InputCore, AirfoilImporter, RenderCore, RHI, OpenCVHelper, OpenCV, Niagara
- Private: AssetTools, UnrealEd, PythonScriptPlugin
- Code optimization is intentionally disabled (`OptimizeCode = CodeOptimization.Never`)

**Active plugins:** PythonScriptPlugin, EditorScriptingUtilities, RawInput, OpenCV, CesiumForUnreal, custom `AirfoilImporter`

## Architecture

### Physics Model: PhysicalAirplane + AerodynamicSurfaceSC

Hierarchical surface decomposition:
```
PhysicalAirplane (APawn)
└── AerodynamicSurfaceSC[]          # wings, tail surfaces
    └── SubAerodynamicSurfaceSC[]   # span-wise segments
        └── ControlSurfaceSC        # ailerons, elevators, rudders
```
Each sub-surface reads its airfoil polar from a DataTable, computes angle of attack from the local airflow vector, looks up CL/CD, and applies force+torque to the aircraft's rigid body. Control surfaces (`EFlapType`: Aileron, Elevator, Rudder) can be deflected by pilot input.

`PhysicalAirplane` also tracks bound vortex filaments (`FBoundVortex`) and trailing wake nodes (`FTrailingVortexNode`). These are **visualization only** — forces come exclusively from DataTable polar lookups, not from the VLM. Wake geometry is pushed each tick to the `FlowVisualizer` (`UNiagaraComponent`) via `SendWakeDataToNiagara()`, where GPU-side Custom HLSL computes Bio-Savart induction for ribbon rendering. See `Docs/Niagara_VLM_Setup.md` (Ukrainian) for Niagara User Parameter bindings and scratch-pad HLSL setup.

### Two-Phase Actor Initialization

`APhysicalAirplane` initializes in two stages:
- **`OnConstruction`** (runs in-editor on placement or transform change): collects all `AerodynamicSurfaceSC` and `ControlSurfaceSC` children, resolves center of mass from the static mesh, passes CoM to each surface, and draws debug crosshairs/labels.
- **`BeginPlay`** (runtime, physics engine active): re-initializes surfaces with the physics-accurate CoM, sets global time dilation from `DebugSimulatorSpeed`, and spawns Niagara flow visualizer components.

Code that depends on physics state (velocity, angular velocity) must run in `BeginPlay` or later — `OnConstruction` runs before the physics engine is active.

### Force Calculation Pattern
`AerodynamicForce` struct holds `PositionalForce` (lift + drag) and `RotationalForce` (torque + r × F). Dynamic pressure = `0.5 * ρ * V²`. Forces are in world space after transforming from surface-local coordinates.

### Aerodynamic Data Pipeline

**XFoil path** (fast, 2D panel method):
1. Design aircraft geometry in **OpenVSP** (`Tools/OpenVSP/`)
2. Generate polars with **XFoil** (`Tools/XFoil/`) via `AerodynamicPhysicalCalculationUtil::CalculatePolar`
3. Extrapolate to 360° AoA using **`Tools/Airfoil/airfoil.py`** (Viterna method via `airfoilprep`)

**SU2 path** (higher-fidelity CFD, primary for production polars):
1. Run `AerodynamicPhysicalCalculationUtil::RunSU2Calculation` from Unreal (calls `Tools/SU2/execute_su2_calculation.py` via PythonScriptPlugin)
2. `sweep.py` runs a full flap-angle polar sweep using SU2-v8.4.0; results cached in `Tools/SU2/logs/`
3. `su2_unreal_import.py` creates DataTable assets under `/Game/` automatically
4. Asset naming: dots in floats become dashes — e.g. `/Game/WingProfile/NACA_0009/DT_naca0009_0-75_-40-0_40-0`
5. For standalone runs (without Unreal): use `Tools/SU2/unrealless_run.py` or `unrealless_run_list.py`

**Common final step:** Import `.dat` files into Unreal via the custom **AirfoilImporter** plugin → becomes a `DataTable` of `FAerodynamicProfileRow` (CL/CD/Cm curves keyed by flap angle)

### Computer Vision
`UUAVCameraComponent` (in `Components/`) manages the onboard camera: it owns the `USceneCaptureComponent2D` (640×480, B8G8R8A8), runs OpenCV processing each frame (flip + text overlay), and exposes a `UTexture2D* OutputTexture` to bind in widgets or materials.

### Utility Class Responsibilities
The `Util/` directory has single-responsibility helpers:

| Class | Role |
|-------|------|
| `AerodynamicPhysicsLibrary` | Stateless aerodynamics math (BlueprintFunctionLibrary): AoA, lift, drag, torque, unit conversions |
| `AerodynamicPhysicalCalculationUtil` | Orchestrates polar generation: dispatches to XFoil (`CalculatePolar`) or SU2 (`RunSU2Calculation`); finds profile `.dat` files; builds/attaches asset paths |
| `AerodynamicProfileLookup` | DataTable row lookup; row naming convention: `FLAP_{angle}_Deg` |
| `AerodynamicToolRunner` | Low-level I/O + external process execution (Python/XFoil/OpenVSP), polar file parsing |
| `AerodynamicUtil` | Airfoil geometry utilities: chord finding, profile scaling/normalization, 2D→3D conversion |
| `ControlInputMapper` | Maps normalized input [-1,1] to flap deflection angles; resolves per-`EFlapType` with mirror support |
| `CoordinateTransformUtil` | Local-to-world coordinate conversion for positions and `FChord` objects |
| `AerodynamicDebugRenderer` | Viewport visualizations: surface outlines, splines, flap hinges, force arrows, labels |
| `TextUtil` | String helper: `RemoveAfterSymbol` strips the last occurrence of a delimiter and everything after it |

### Key Files
| File | Role |
|------|------|
| `Actor/PhysicalAirplane.h/cpp` | Main aircraft pawn; control inputs, vortex wake tracking, Niagara wake visualization |
| `Components/UAVCameraComponent.h/cpp` | Onboard camera + OpenCV processing component |
| `Components/UAVPhysicsStateComponent.h/cpp` | Per-tick physics state cache (velocity, CoM, airflow); call `Update()` before reading |
| `SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h/cpp` | Per-surface lift/drag computation |
| `SceneComponent/SubAerodynamicSurface/SubAerodynamicSurfaceSC.h/cpp` | Per-segment forces, reads DataTable |
| `SceneComponent/ControlSurface/ControlSurfaceSC.h/cpp` | Deflectable control surfaces |
| `Structure/AerodynamicSurfaceStructure.h` | `FAerodynamicSurfaceStructure` USTRUCT: chord size, offsets, flap range, DataTable ref, `EFlapType` |
| `Entity/VortexEntities.h` | `FBoundVortex` and `FTrailingVortexNode` structs for vortex wake |
| `Entity/` | Plain C++ structs (POD): `AerodynamicForce`, `Chord`, `PolarRow`, `ControlInputState`, etc. |
| `Plugins/AirfoilImporter/` | Custom editor plugin: `.dat` → DataTable factory |

## Physics Configuration

`Config/DefaultEngine.ini` enables physics substepping:
- Max substeps: 16
- Max substep delta time: 0.0013 s (≈769 Hz)

Simulation speed can be scaled at runtime via `DebugSimulatorSpeed` on `PhysicalAirplane` (uses Unreal time dilation).

## Debug & Development

`PhysicalAirplane` exposes:
- `DebugConsoleLogs` — prints force values to output log
- `bDrawDebugMarkers` — draws force vectors in viewport

No automated test framework is set up. Testing is done by running the editor and observing simulation behavior with debug markers enabled.

## Notes

- UI-facing property names and some comments are in **Ukrainian** (the development team's language).
- `Tools/Airfoil/airfoil.py` requires `airfoilprep` Python package; `Tools/PyInstall/` and `Tools/OpenVSP/install_to_py.bat` handle dependency setup.
- SU2 hardcoded defaults (set in `RunSU2Calculation`): CoreNumber=4, HingeLocation=0.75, FlapStep=1.0, RmsQuality=-4, Resume=true.
- `AerodynamicToolRunner` enforces a **45-character max on temp directory paths** — XFoil and SU2 binaries fail silently on longer Windows paths. Keep the project root path short.
- Standalone SU2 (no Unreal required): `Tools/SU2/run_unrealless.bat` wraps `unrealless_run.py` / `unrealless_run_list.py`.
- The large file `Saved/Cesium/...` (SQLite cache ~305 MB) is geospatial tile cache — do not commit.
