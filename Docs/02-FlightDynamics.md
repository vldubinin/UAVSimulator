# 02 — Льотна динаміка

Уся аеродинаміка, фізичні сили, вихоровий слід та тяга живуть у
`UFlightDynamicsComponent` (`Components/FlightDynamicsComponent.h/.cpp`) і в
ієрархії аеродинамічних поверхонь під ним.

## Ієрархія аеродинамічних поверхонь

```
AAirplane (APawn)
└── UAerodynamicSurfaceSC[]           # крила, оперення (USceneComponent, додаються в Blueprint)
    └── USubAerodynamicSurfaceSC[]    # секції вздовж розмаху (генеруються в OnConstruction)
        ⇅ прив'язка за типом/дзеркальністю
UControlSurfaceSC                     # елерони, рулі висоти/напрямку (USceneComponent, у Blueprint)
```

### `UAerodynamicSurfaceSC`

`SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h/.cpp`.

**Властивості (EditAnywhere):**
- `Profile : UDataTable*` — таблиця точок профілю крила (`FAirfoilPointData`).
- `SurfaceForm : TArray<FAerodynamicSurfaceStructure>` — форма поверхні: список
  «станцій» уздовж розмаху (`ChordSize`, `Offset`, положення й межі закрилка,
  `AerodynamicTable`, `FlapType`). Для N секцій потрібно N+1 записів.
- `AerodynamicCenterOffsetPercent` — зсув аеродинамічного центру вздовж хорди (%).
- `Mirror : bool` — симетрична поверхня (дзеркалиться по розмаху).
- `Enable : bool`.

**`OnConstruction(CoM, ControlSurfaces)`** — знищує старі підповерхні
(`DestroySubsurfaces`), будує нові з `SurfaceForm` (`BuildSubsurfaces(CoM, +1)`,
і `-1` якщо `Mirror`). Кожна секція:
- нормалізує профіль (інверсія X), масштабує до `ChordSize`, переводить у 3D із
  накопиченим `GlobalOffset` (по Y множиться на `Direction` для дзеркала);
- шукає відповідний `UControlSurfaceSC` за `FlapType` + дзеркальністю
  (`FindControlSurface`);
- викликає `USubAerodynamicSurfaceSC::InitComponent(...)`.

**`CalculateForcesOnSurface(...)`** — підсумовує `FAerodynamicForce` (позиційна +
обертальна) від усіх підповерхонь.

**`SetNiagaraActive(bool)`** — `Activate`/`Deactivate` для всіх
`UNiagaraComponent`, прикріплених дітьми до цієї поверхні.

### `USubAerodynamicSurfaceSC`

`SceneComponent/SubAerodynamicSurface/SubAerodynamicSurfaceSC.h/.cpp`. Одна
секція між двома станціями `SurfaceForm`.

**`InitComponent(...)`** — зберігає 3D-профілі початку/кінця, знаходить хорди
(`AerodynamicUtil::FindChord` за екстремумами X), центр тиску
(`UAerodynamicPhysicsLibrary::FindCenterOfPressure`), площу секції
(`CalculateQuadSurfaceArea`, см²), відстань ЦТ→ЦМ, межі й положення закрилка,
`EFlapType`, посилання на `UControlSurfaceSC`. Малює редакторну візуалізацію
(`AerodynamicDebugRenderer`: контур поверхні, шарнір закрилка, перехрестя ЦТ,
мітки площі та відстані до ЦМ).

**`CalculateForcesOnSubSurface(LinearVelocity, AngularVelocity, CoMWorld,
AirflowDirection, ControlState, bVisualizeForces, DeltaTime)`** за кадр:
1. Переводить збережені локальні хорди у світ; усереднює напрямок хорди.
2. `RelativePosition = CoP_world − CoM_world`; `RotationalVelocity = ω × r`;
   **`Wind`** — живе значення (не заглушка): запит до
   `GetWorld()->GetSubsystem<UUAVSimulationSubsystem>()->GetWindVelocityAtLocation(
   CenterOfPressureInWorld)` **саме в точці цього сегмента** — крило в пориві й
   фюзеляж поза ним отримують коректно різний внесок. Єдине місце в
   аеродинамічному конвеєрі, що знає про вітер — деталі формування `Wind`
   (зона/напрямок/загасання на `AWindActor`, векторна сума кількох векторів на
   підсистемі) — в `13-Environment-Actors.md`.
   `WorldAirVelocity = −V_linear + Wind − RotationalVelocity`.

   (Не плутати з параметром `AirflowDirection`, що теж приходить у цю функцію
   від `UUAVPhysicsStateComponent::GetAirflowDirection()` — він тут **мертвий**,
   ніде в тілі не читається; реальний вплив на політ — лише через локальний
   `Wind` вище.)
3. `Speed` (м/с), `AoA` (`CalculateAngleOfAttack`, `atan2` проекцій потоку на
   up- та chord-вектори), `q = 0.5·ρ·V²` (ρ = 1.225, `static constexpr`).
4. `CommandedAngle = ControlInputMapper::ResolveFlapAngle(FlapType, IsMirror,
   Min, Max, ControlState)` — **без завчасного округлення**.
5. Якщо є `ControlSurface` — `PhysicalAngle = ControlSurface->Move(CommandedAngle,
   DeltaTime)` (перехідний процес приводу, див. нижче). Саме **фактичний**, а не
   командний кут визначає силу. `Clamp` до `[Min, Max]`, тоді `RoundToInt`.
6. `AerodynamicProfileLookup::FindProfile(AerodynamicTable, FlapAngle)` — рядок
   `FLAP_{angle}_Deg`; `nullptr` (сили 0), якщо таблиця/рядок відсутні.
7. `CalculateLift/Drag/Torque` (Н) → `NewtonsToKiloCentimeter` (1 Н = 100
   kg·cm/s²). Момент Н·м → внутрішні через `×10000`.
8. `FAerodynamicForce(LiftForce, DragForce, TorqueForce, RelativePosition)` —
   конструктор одразу рахує `RotationalForce = Torque + r × (Lift+Drag)`.

`FAirDensity` тут — окрема константа секції (1.225), не плутати з
`UFlightDynamicsComponent::AirDensity` (теж 1.225, EditAnywhere, вживається для Γ).

### `UControlSurfaceSC`

`SceneComponent/ControlSurface/ControlSurfaceSC.h/.cpp`.

**Властивості:** `FlapType : EFlapType`, `AxisType : EAxisType` (X=крен, Y=тангаж,
Z=рискання), `IsMirror : bool`, `IsReverseDirection : bool`,
`Actuator : FActuatorDynamics`, `bLogAngleDebug`.

**`Move(TargetAngle, DeltaTime) → float`**:
- при першому виклику запам'ятовує монтажну орієнтацію (`RestRelativeRotation`) —
  авторський нахил під V-подібність / стрілоподібність;
- `LogicalAngle = Actuator.Advance(TargetAngle, DeltaTime)` — перехідний процес;
- `VisualAngle = IsReverseDirection ? −LogicalAngle : LogicalAngle` (інверсія лише
  для візуального обертання);
- `SetRelativeRotation(Rest * Deflection.Quaternion())` навколо осі `AxisType`;
- повертає `LogicalAngle` («логічні» градуси) — його бере
  `USubAerodynamicSurfaceSC` для пошуку профілю.

`GetCurrentAngle()` → `Actuator.Angle`.

## Динаміка приводу керма — `FActuatorDynamics`

`Entity/ActuatorDynamics.h/.cpp`. Перетворює миттєвий командний кут на фактичний
фізичний кут із перехідним процесом. `Advance(TargetAngle, DeltaTime) → Angle`.

`EActuatorDynamicsModel`:

| Модель | Поведінка |
|--------|-----------|
| `RateLimitedOnly` | Лише обмеження швидкості `MaxSlewRateDegPerSec` (град/с). Безумовно стійка за будь-якого DeltaTime. **За замовчуванням.** |
| `FirstOrderLag` | `FMath::FInterpTo` зі сталою часу `TimeConstantSeconds`, додатково обмежене швидкістю. Той самий прийом, що розкрутка двигуна |
| `CriticallyDampedSecondOrder` | Маса-пружина-демпфер: `NaturalFrequencyHz`, `DampingRatio` (1.0 = критичне). Напів-неявний Ейлер із під-кроками `MaxSubDt = 1/240`, швидкість клампиться до `MaxSlewRateDegPerSec` |

Стан (`VisibleAnywhere`): `Angle`, `Velocity`. `Reset(InitialAngle)`.

## `UFlightDynamicsComponent`

### Публічний контроль-API (пише лише `UPilotInputComponent`)

| Метод | Дія |
|-------|-----|
| `UpdateAileronControl(L, R)` | `ControlState.Left/RightAileronAngle` |
| `UpdateElevatorControl(L, R)` | `ControlState.Left/RightElevatorAngle` |
| `UpdateRudderControl(angle)` | `ControlState.RudderAngle` |
| `UpdateThrottleControl(throttle)` | `TargetThrottle = Clamp(throttle, 0, 1)` |

`ControlState` (`FControlInputState`, `Entity/ControlInputState.h`) обнуляється
наприкінці кожного `TickComponent` — нові значення мають надійти наступного кадру
(звідси порядок тіку).

### Геттери стану (з `UUAVPhysicsStateComponent`)

| Метод | Значення |
|-------|----------|
| `GetAirspeed()` | `PhysicsState->GetLinearVelocity().Size() / 100` — м/с |
| `GetAirspeedKmh()` | те саме × 3.6 |
| `GetAngleOfAttack()` | кут між `ActorForward` і вектором швидкості, градуси |
| `GetLeftWingtipWorldPosition()` | `CurrentBoundVortices[0].StartPoint` |
| `GetRightWingtipWorldPosition()` | `CurrentRightWingtipWorldPos` (дзеркальна точка) |
| `GetDesignWingSpanCm()` | сира сума `|Offset.Y|` по `Surfaces[0].SurfaceForm` (× 2 якщо `Mirror`), **без** масштабу актора — для авто-калібрування |
| `GetVortexWakeLines()` | `const TArray<TArray<FTrailingVortexNode>>&` — для Niagara-візуалізатора |

Валідні лише після тіку `UFlightDynamicsComponent` у поточному кадрі. Споживач
має додати `AddTickPrerequisiteComponent(DynamicsComp)` у `BeginPlay`.

### Двигун

| Властивість | Замовч. | Роль |
|-------------|---------|------|
| `MaxStaticThrust` | 1 500 000 | Макс. статична тяга (внутр. одиниці) |
| `EngineSpoolSpeed` | 1.5 | Швидкість розкрутки (`FInterpTo`) |
| `ThrustVsAirspeedCurve : UCurveFloat*` | — | Множник тяги від повітряної швидкості (без кривої = 1.0) |
| `EngineThrustOffsetLocal : FVector` | 0 | Точка прикладання тяги (лок. коорд.) |
| `TargetThrottle` / `CurrentThrottle` | 0 | Ціль / фактичний газ (`CurrentThrottle` інерційно наздоганяє `TargetThrottle`) |
| `InitialSpeedMs` | 0 | Початкова швидкість при старті (задається як `SetPhysicsLinearVelocity` вперед) |

### VLM / вихоровий слід

| Властивість | Замовч. | Роль |
|-------------|---------|------|
| `MaxWakeLength` | 100 | Макс. вузлів в одній лінії сліду |
| `MinWakeDistance` | 50 | Мін. відстань (см) між вузлами |
| `AirDensity` | 1.225 | ρ (кг/м³) для розрахунку циркуляції Γ |

**`TickComponent` (за кадр), коли `Mesh->IsSimulatingPhysics()`:**

1. `PhysicsState->Update()`.
2. Для кожної `UAerodynamicSurfaceSC`:
   - `SurfaceForce = Surface->CalculateForcesOnSurface(...)`, підсумовується в
     `TotalForce`.
   - Розмах секції `SpanCm` рахується вручну з `|Form.Offset.Y|` × масштаб
     `Mesh` по Y (× 2 якщо `Mirror`).
   - **Зворотня Кутта-Жуковського:** `Γ = F / (ρ · V · b)`.
   - `SurfaceIdx == 0` → оновлює `CurrentRightWingtipWorldPos`.
   - **Еліптичний розподіл циркуляції** по розмаху: `GammaMax = Γ · 4/π`,
     `NumSegments = Clamp(round(SpanM/0.5), 2, 20)`, для кожного сегмента:
     `LocalGamma = GammaMax · sqrt(1 − (y/halfSpan)²)`.
   - **Фізика індуктивного опору:** `Vind = GetInducedVelocity(SegmentCenter)`
     (сума Біо-Савара по всіх лініях сліду, метри); теорема Кутти-Жуковського
     `F = ρ · Γ · (dl × Vind)`; `Mesh->AddForceAtLocation(F_UU, SegmentCenter)`.
   - Додає `FBoundVortex{StartPoint, EndPoint, LocalGamma}` в `CurrentBoundVortices`.
3. `Mesh->AddForce(TotalForce.PositionalForce)` +
   `AddTorqueInRadians(TotalForce.RotationalForce)`.
4. Розкрутка двигуна: `CurrentThrottle = FInterpTo(..., EngineSpoolSpeed)`. Якщо
   `> 0.01`: `ActualThrust = MaxStaticThrust · CurrentThrottle · ThrustMultiplier`;
   `AddForceAtLocation(Forward · ActualThrust, ThrustLocationWorld)`.
5. Якщо `bLogFlightDebug` (акумулятор 0.5 с) — друкує в `LogUAV` + на екран:
   швидкість (км/год, гор., верт.), AoA, газ ц/факт, K тяги, тяга (Н), опір
   поляри / індуктивний / разом (Н), підйом (Н), вага (Н), маса (кг), а також
   `[CoMDebug]` — позиція ЦМ і відстані до поверхонь.
6. `UpdateVortexWake()`.
7. `ControlState = FControlInputState()` (обнулення).

**`UpdateVortexWake()`** — на кожен приєднаний вихор веде 2 лінії сліду (корінь
`+Γ`, кінцівка `−Γ`; правило правої руки Root→Tip). Новий `FTrailingVortexNode`
додається, лише якщо відстань до останнього ≥ `MinWakeDistance`; при переповненні
`MaxWakeLength` — видаляється найстаріший.

**`GetInducedVelocity(TargetPosCm) → FVector` (м/с)** — сума внесків усіх
сегментів усіх ліній сліду за законом Біо-Савара для скінченного відрізка
(`dV = Γ/(4π·|R1×R2|²) · (R1×R2) · ((r1+r2)(1 − R1·R2/(r1·r2)))`), із захистом
від сингулярності (`|R1×R2|² > 0.001`).

### Прапорці Simulation Settings

- `DebugSimulatorSpeed` (1.0) — множник глобального `TimeDilation`.
- `bVisualizeForces` — Debug Arrows векторів сил/моментів (зелена стрілка
  результуючої сили на кожному сегменті, `DrawDebugDirectionalArrow`) **плюс**
  блакитна стрілка вітру в тій самій точці, якщо там ненульовий `Wind`
  (`AerodynamicDebugRenderer::DrawForceArrow`).
- `bLogFlightDebug` (true) — діагностичний лог розгону (див. вище).
- `GenerateAerodynamicPhysicalConfigutation()` (`CallInEditor`, «Розрахувати
  поляри для ЛА») → `AerodynamicPhysicalCalculationUtil::...` (див. `10-Aero-Data-Pipeline.md`).

### `UpdateEditorVisualization(Mesh)`

Замінює стару логіку `OnConstruction`: збирає поверхні/керма, ре-ініціалізує їх з
CoM (`Mesh->GetCenterOfMass()` в редакторі — 0), малює перехрестя й мітку точки
тяги.

## `UUAVPhysicsStateComponent`

`Components/UAVPhysicsStateComponent.h/.cpp` — субоб'єкт
`UFlightDynamicsComponent`, оновлюється на початку його тіку. Не тікає сам.
`Update()` кешує з `UStaticMeshComponent` власника:

| Геттер | Значення | Коли фізика вимкнена |
|--------|----------|----------------------|
| `GetLinearVelocity()` | cm/s, світ | лишається останнім |
| `GetAngularVelocity()` | рад/с, світ | `ZeroVector` |
| `GetCenterOfMass()` | cm, світ | `ZeroVector` |
| `GetAirflowDirection()` | нормалізований, протилежний `Owner->GetVelocity()` | `ZeroVector` |

## Структури-entity

- `FAerodynamicForce` (`Entity/AerodynamicForce.h`) — `PositionalForce` (Lift+Drag),
  `RotationalForce` (Torque + r×F).
- `FControlInputState` (`Entity/ControlInputState.h`) — 5 кутів
  (L/R елерон, L/R руль висоти, руль напрямку).
- `FBoundVortex` / `FTrailingVortexNode` (`Entity/VortexEntities.h`) —
  `StartPoint`/`EndPoint`/`Gamma` (м²/с) та `Position`/`Gamma`.
- `FChord` (`Entity/Chord.h`) — `StartPoint`, `EndPoint`, `Length` (= `EndX − StartX`).
- `FPolarRow` (`Entity/PolarRow.h`) — `CL`, `CD`, `CM`.
- `EFlapType` — `None`, `Aileron`, `Elevator`, `Rudder`.
- `EAxisType` — `X`, `Y`, `Z`.
- `FAerodynamicSurfaceStructure` (`Structure/`) — `FTableRowBase`: `ChordSize`,
  `Offset`, `Start/EndFlapPosition`, `Min/MaxFlapAngle`, `AerodynamicTable`,
  `FlapType`.
- `FAerodynamicProfileRow` (`DataAsset/`) — `FTableRowBase`: `FlapAngle`,
  `ClVsAoA`, `CdVsAoA`, `CmVsAoA` (`FRuntimeFloatCurve`). `FAerodynamicProfileAndFlapRow`
  обгортає рядок разом із `FlapAngle`.

## Утилітні класи

| Клас | Роль |
|------|------|
| `UAerodynamicPhysicsLibrary` | Безстанова математика (BlueprintFunctionLibrary): `CalculateAngleOfAttack`, `CalculateLift/Drag/Torque`, `VelocityToMetersPerSecond`, `NewtonsToKiloCentimeter` (×100), `CalculateAverageChordLength`, `CalculateLiftDirection`, `FindCenterOfPressure`, `CalculateQuadSurfaceArea`, `GetPointOnLineAtPercentage` |
| `AerodynamicPhysicalCalculationUtil` | Оркестрація генерації полярів: XFoil (`CalculatePolar`) або SU2 (`RunSU2Calculation`), пошук `.dat`, побудова/перевірка/прив'язка `DataTable`-ресурсів. Див. `10-Aero-Data-Pipeline.md` |
| `AerodynamicProfileLookup` | Пошук рядка `DataTable` за форматом `FLAP_{angle}_Deg` |
| `AerodynamicToolRunner` | Низькорівневий I/O + запуск зовнішніх процесів (Python/XFoil/OpenVSP), парсинг полярів. **Ліміт 45 символів на шлях тимчасової теки** — довші ламають XFoil/SU2 на Windows |
| `AerodynamicUtil` | Геометрія профілю: `FindChord`, `Scale`, `NormalizePoints` (інверсія X), `ConvertTo3DPoints`, `AdaptTo` |
| `ControlInputMapper` | `MapInputToFlapAngle([-1,1] → [Min,Max])`, `ResolveFlapAngle(FlapType, bMirror, ...)` — розводить нормалізований сигнал по каналах L/R/Rudder |
| `CoordinateTransformUtil` | Local↔World для позицій і `FChord` |
| `AerodynamicDebugRenderer` | Редакторні візуалізації: контури поверхонь, сплайни, шарніри закрилків, стрілки сил, мітки |
| `TextUtil` | `RemoveAfterSymbol` — обрізає все після останнього входження роздільника |

## Візуалізація вихорового сліду — `UAeroVisualizerComponent`

`SceneComponent/AeroVisualizer/AeroVisualizerComponent.h/.cpp`. `USceneComponent`,
тікає в `TG_PostPhysics` із prerequisite на `UFlightDynamicsComponent`.

- `FlowVisualizerSystem : UNiagaraSystem*` (EditAnywhere).
- **`BeginPlay`** — для кожної `UAerodynamicSurfaceSC`, ім'я якої містить `Wing`
  або `TailHorizontal`, спавнить `UNiagaraComponent` (кріпить до поверхні,
  `bAutoActivate = false`), рахує `SpanCm` (× масштаб `Mesh` по Y), задає
  параметри `SurfaceSpan` і `ProbeHeight` (для `TailHorizontal` — зсув +100 см,
  span × 1.5, probe 150; для крила — probe 2.0).
- **`TickComponent`** → `UpdateNiagaraWakeData()`: сплющує `GetVortexWakeLines()`
  у `TArray<FVector> WakePositions` + `TArray<float> WakeGammas`, після кожної
  лінії додає sentinel-вузол (`+Z 100000`, Γ = 0) — сигнал кінця стрічки для
  Niagara; штовхає масиви через
  `UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector/Float`.

Ручне налаштування самої Niagara-системи (GPU, Custom HLSL Біо-Савара) — у
[12-Niagara_VLM_Setup.md](12-Niagara_VLM_Setup.md).
