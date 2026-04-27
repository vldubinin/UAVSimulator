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

### Main Actor: AAirplane

`AAirplane` (`Actor/Airplane.h/cpp`) is the sole aircraft pawn. `PhysicalAirplane` has been deleted — do not reference it. The actor owns two CDO components:

- **`UFlightDynamicsComponent`** — all aerodynamics, physics forces, vortex wake, engine thrust
- **`UUAVCameraComponent`** — onboard OpenCV camera (is a `UActorComponent`, not a `USceneComponent`; cannot be attached to a scene hierarchy)

### Aerodynamic Surface Hierarchy

```
AAirplane (APawn)
└── UAerodynamicSurfaceSC[]          # wings, tail surfaces (USceneComponent)
    └── USubAerodynamicSurfaceSC[]   # span-wise segments
        └── UControlSurfaceSC        # ailerons, elevators, rudders
```

Each `USubAerodynamicSurfaceSC` reads its airfoil polar from a DataTable, computes AoA from the local airflow vector, looks up CL/CD, and applies force+torque via `FAerodynamicForce`. Control surfaces (`EFlapType`: Aileron, Elevator, Rudder) can be deflected by pilot input.

After each `CalculateForcesOnSubSurface` call, the sub-surface caches two public fields for downstream use:
- `CurrentWorldCoP` — world-space center of pressure
- `CurrentGamma` — bound vortex circulation (m²/s) via Kutta-Joukowski: `Γ = L / (ρ · V · b)`

`UAerodynamicSurfaceSC` aggregates these via `GetSubSurfacePositions()` / `GetSubSurfaceGammas()`. `UFlightDynamicsComponent` further aggregates across all surfaces via `GetAllVortexPositions()` / `GetAllVortexGammas()`.

### Two-Phase Actor Initialization

`AAirplane` initializes in two stages:
- **`OnConstruction`** (runs in-editor on placement or transform change): collects `AerodynamicSurfaceSC` / `ControlSurfaceSC` children, resolves CoM from static mesh, draws debug labels.
- **`BeginPlay`** (runtime, physics active): re-initializes surfaces with physics-accurate CoM, sets time dilation from `DebugSimulatorSpeed`, spawns Niagara flow visualizers.

Code that reads velocity or angular velocity must run in `BeginPlay` or later.

### FlightDynamicsComponent Public API

Getters backed by `UUAVPhysicsStateComponent` (updated at the top of each tick):
- `GetAirspeed()` — speed in m/s
- `GetAngleOfAttack()` — body-level AoA in degrees (velocity vs. actor forward)
- `GetRelativeWindVector()` — normalized airflow direction (opposite to velocity)
- `GetAllVortexPositions()` — flat array of `CurrentWorldCoP` from all sub-surfaces
- `GetAllVortexGammas()` — flat array of `CurrentGamma` from all sub-surfaces

These are only valid after `UFlightDynamicsComponent` has ticked in the current frame. Any component that reads them must call `AddTickPrerequisiteComponent(DynamicsComp)` in `BeginPlay`.

### VFX Data Pipeline

`UAerodynamicVFXComponent` (`Components/AerodynamicVFXComponent.cpp`) is a `USceneComponent` that drives the Niagara VLM visualization:

1. **Lazy spawn** — Niagara is not spawned in `BeginPlay`. `TickComponent` checks `ShouldShowVLMVFX()` on the GameMode and `IsLocallyControlled()` on the owning pawn each tick. The system spawns on the first tick both conditions are true, and is destroyed if either becomes false. This handles deferred `Possess` calls correctly.
2. **Coordinate conversion** — `GetAllVortexPositions()` returns world-space coordinates. Before passing to Niagara (which is attached with `KeepRelativeOffset`), each position is converted to actor-local space via `GetActorTransform().Inverse().TransformPosition(...)`.
3. **Per-tick push** — scalars (`AoA`, `Airspeed`, `AirflowDirection`) via `SetVariableFloat`/`SetVariableVec3`; arrays (`WakePositions`, `WakeGammas`) via `UNiagaraDataInterfaceArrayFunctionLibrary`.

### Simulator Modes (AUAVSimulatorGameModeBase)

`ESimulatorMode` controls `BeginPlay` behavior:

- **`RecordTarget`** — spawns `TargetAirplaneClass` at `PlayerStart`, dynamically attaches `UFlightRecorderComponent`, calls `StartRecording()`.
- **`PlaybackAndTrack`** — loads `FlightScenarioSave` from `ScenarioSlotName`, spawns both target and tracker airplanes. The target gets a dynamic `UFlightPlaybackComponent`; the player controller possesses the tracker.

**Critical:** dynamically created components (`NewObject<UFlightPlaybackComponent>`) must call `AddInstanceComponent(Comp)` on the owning actor **before** `RegisterComponent()`, otherwise the engine skips their `TickComponent`.

### Playback Subsystem

`UFlightPlaybackComponent::StartPlayback()` disables competing systems on the target airplane in this order:
1. `UStaticMeshComponent::SetSimulatePhysics(false)` — stops the physics solver from fighting playback transforms
2. `UUAVPhysicsStateComponent::SetComponentTickEnabled(false)` — stops the custom physics cache from overwriting state
3. `UFlightDynamicsComponent::SetComponentTickEnabled(false)` + `SetActive(false)` — silences force application
4. `UUAVCameraComponent::SetComponentTickEnabled(false)` + `SetActive(false)` — camera is unused during playback

`TickComponent` interpolates between recorded `FFlightFrame` samples using `FQuat::Slerp` for rotation and `FMath::Lerp` for position, applying `PlaybackOffset` to shift the trajectory.

### Physics State Cache

`UUAVPhysicsStateComponent` caches `LinearVelocity`, `AngularVelocity`, `CenterOfMassInWorld`, and `AirflowDirection` each tick. Call `Update()` once per tick before reading any getter. It is a subobject of `UFlightDynamicsComponent` and is updated at the top of its `TickComponent`.

### Force Calculation Pattern

`FAerodynamicForce` holds `PositionalForce` (lift + drag) and `RotationalForce` (torque + r × F). Dynamic pressure = `0.5 * ρ * V²`. Forces are computed in Newtons and converted to Unreal internal units (`kg·cm/s²`) via `NewtonsToKiloCentimeter` before being applied to the mesh.

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

**Common final step:** Import `.dat` files into Unreal via the custom **AirfoilImporter** plugin → becomes a `DataTable` of `FAerodynamicProfileRow` (CL/CD/Cm curves keyed by flap angle)

### Computer Vision
`UUAVCameraComponent` (in `Components/`) manages the onboard camera: owns `USceneCaptureComponent2D` (640×480, B8G8R8A8), runs OpenCV processing each frame (flip + text overlay), exposes `UTexture2D* OutputTexture`. It is a `UActorComponent`, not a `USceneComponent` — it has no transform and cannot be placed in the scene hierarchy.

### Utility Class Responsibilities

| Class | Role |
|-------|------|
| `AerodynamicPhysicsLibrary` | Stateless aerodynamics math (BlueprintFunctionLibrary): AoA, lift, drag, torque, unit conversions |
| `AerodynamicPhysicalCalculationUtil` | Orchestrates polar generation: dispatches to XFoil (`CalculatePolar`) or SU2 (`RunSU2Calculation`) |
| `AerodynamicProfileLookup` | DataTable row lookup; row naming convention: `FLAP_{angle}_Deg` |
| `AerodynamicToolRunner` | Low-level I/O + external process execution (Python/XFoil/OpenVSP), polar file parsing |
| `AerodynamicUtil` | Airfoil geometry utilities: chord finding, profile scaling/normalization, 2D→3D conversion |
| `ControlInputMapper` | Maps normalized input [-1,1] to flap deflection angles; resolves per-`EFlapType` with mirror support |
| `CoordinateTransformUtil` | Local-to-world coordinate conversion for positions and `FChord` objects |
| `AerodynamicDebugRenderer` | Viewport visualizations: surface outlines, splines, flap hinges, force arrows, labels |
| `TextUtil` | `RemoveAfterSymbol` strips the last occurrence of a delimiter and everything after it |

### Key Files

| File | Role |
|------|------|
| `Actor/Airplane.h/cpp` | Main aircraft pawn; owns `FlightDynamicsComponent` and `UAVCameraComponent` |
| `Components/FlightDynamicsComponent.h/cpp` | All aerodynamics, physics forces, vortex wake, Niagara flow visualizers |
| `Components/AerodynamicVFXComponent.cpp` | Niagara VLM VFX: lazy spawn, local-space coordinate conversion, per-tick data push |
| `Components/UAVPhysicsStateComponent.h/cpp` | Per-tick physics state cache; call `Update()` before reading |
| `Components/FlightPlaybackComponent.h/cpp` | Interpolated playback of recorded `FFlightFrame` data; disables physics/dynamics on owner |
| `Components/FlightRecorderComponent.h/cpp` | Records flight frames to `UFlightScenarioSave` save game |
| `Components/UAVCameraComponent.h/cpp` | Onboard camera + OpenCV processing (`UActorComponent`, no scene transform) |
| `SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h/cpp` | Per-surface force aggregation; builds and owns `USubAerodynamicSurfaceSC` children |
| `SceneComponent/SubAerodynamicSurface/SubAerodynamicSurfaceSC.h/cpp` | Per-segment forces, DataTable lookup; exposes `CurrentWorldCoP` and `CurrentGamma` |
| `SceneComponent/ControlSurface/ControlSurfaceSC.h/cpp` | Deflectable control surfaces |
| `UAVSimulatorGameModeBase.h/cpp` | Simulator mode dispatch: `RecordTarget` vs `PlaybackAndTrack` |
| `Structure/AerodynamicSurfaceStructure.h` | `FAerodynamicSurfaceStructure` USTRUCT: chord size, offsets, flap range, DataTable ref, `EFlapType` |
| `Entity/VortexEntities.h` | `FBoundVortex` and `FTrailingVortexNode` structs for vortex wake |
| `Plugins/AirfoilImporter/` | Custom editor plugin: `.dat` → DataTable factory |

## Physics Configuration

`Config/DefaultEngine.ini` enables physics substepping:
- Max substeps: 16
- Max substep delta time: 0.0013 s (≈769 Hz)

Simulation speed can be scaled at runtime via `DebugSimulatorSpeed` on `UFlightDynamicsComponent`.

## Notes

- UI-facing property names and some comments are in **Ukrainian** (the development team's language).
- `Tools/Airfoil/airfoil.py` requires `airfoilprep` Python package; `Tools/PyInstall/` and `Tools/OpenVSP/install_to_py.bat` handle dependency setup.
- SU2 hardcoded defaults (set in `RunSU2Calculation`): CoreNumber=4, HingeLocation=0.75, FlapStep=1.0, RmsQuality=-4, Resume=true.
- `AerodynamicToolRunner` enforces a **45-character max on temp directory paths** — XFoil and SU2 binaries fail silently on longer Windows paths.
- Standalone SU2 (no Unreal required): `Tools/SU2/run_unrealless.bat` wraps `unrealless_run.py` / `unrealless_run_list.py`.
- The large file `Saved/Cesium/...` (SQLite cache ~305 MB) is geospatial tile cache — do not commit.
- `APawn` does not initialize a `RootComponent` by default. When adding `USceneComponent` subobjects to `AAirplane` or any `APawn` subclass, always create an explicit root first (`CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRoot"))`, assign to `RootComponent`) before calling `SetupAttachment`.
- `GEngine->AddOnScreenDebugMessage` requires the `Key` argument to be explicitly cast to `uint64` to avoid overload ambiguity in UE5: use `(uint64)GetOwner()->GetUniqueID()` or `(uint64)-1`.
