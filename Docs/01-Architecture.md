# 01 — Архітектура

## Модуль і залежності збірки

Єдиний ігровий модуль — `UAVSimulator` (`Source/UAVSimulator/`). Редакторні
таргети — `UAVSimulator.Target.cs` / `UAVSimulatorEditor.Target.cs`.

`Source/UAVSimulator/UAVSimulator.Build.cs`:

- **Public**: `Core`, `CoreUObject`, `Engine`, `InputCore`, `AirfoilImporter`,
  `RenderCore`, `RHI`, `OpenCVHelper`, `OpenCV`, `Niagara`, `UMG`, `ZeroMQ`,
  `ImageWrapper`, `Json`
- **Private**: `AssetTools`, `UnrealEd`, `PythonScriptPlugin`, `CesiumRuntime`,
  `Slate`, `SlateCore`
- `OptimizeCode = CodeOptimization.Never` — оптимізація коду вимкнена навмисно
  (зручність налагодження фізики).

**Активні плагіни** (`UAVSimulator.uproject`): `PythonScriptPlugin`,
`EditorScriptingUtilities`, `VisualStudioTools`, `RawInput`, `OpenCV`,
`CesiumForUnreal`, власний `AirfoilImporter`, власний `Unreal5ZeroMQ`
(`Plugins/Unreal5ZeroMQ`, обгортка libzmq 4.3.x).

**Лог-категорія**: `LogUAV` (`Source/UAVSimulator/UAVSimulator.h`).

**Фізика** (`Config/DefaultEngine.ini`): субстепінг увімкнено, `MaxSubsteps=16`,
`MaxSubstepDeltaTime=0.0013` (≈769 Гц). `r.CustomDepth=3` — для конвеєра маски
сегментації, не відкочувати. Швидкість симуляції масштабується в рантаймі через
`UFlightDynamicsComponent::DebugSimulatorSpeed` (застосовується як
`SetGlobalTimeDilation` у `BeginPlay`).

## Головний actor: `AAirplane`

`AAirplane` (`Actor/Airplane.h/.cpp`) — єдиний pawn літака. `APawn` не створює
`RootComponent` за замовчуванням; будь-який `USceneComponent`-субоб'єкт вимагає
явного кореня перед `SetupAttachment`.

### CDO-компоненти (створюються в конструкторі)

| Компонент | Роль |
|-----------|------|
| `UFlightDynamicsComponent` | Уся аеродинаміка, фізичні сили, вихоровий слід, тяга двигуна. Містить субоб'єкт `UUAVPhysicsStateComponent` |
| `UAttitudeControlComponent` | Автопілот на ZMQ; `IPilotInputSource` тир 100. **Неактивний**, доки хтось не викличе `ActivateAutopilot()` |
| `UPilotInputComponent` | Координатор усього керування — єдиний, хто пише контрольний API `UFlightDynamicsComponent` |
| `UKeyboardPilotInputComponent` | `IPilotInputSource` тир 0, осі `Kbd{Roll,Pitch,Yaw,Throttle}` |
| `UGamepadPilotInputComponent` | `IPilotInputSource` тир 0, осі `Pad{Roll,Pitch,Yaw,Throttle}` |

### Компоненти, що НЕ є CDO

- `UUAVCameraComponent` — створюється ліниво в `RefreshConfigurations()`, коли
  камера вмикається для ролі цього літака. Це `UActorComponent` (не
  `USceneComponent`) — без трансформу, не кріпиться до сцени.
  `AAirplane::GetCameraOutputTexture()` віддає `UUAVCameraComponent::OutputTexture`.
- Сенсорні компоненти, `USensorBusComponent`, `UAeroVisualizerComponent`,
  аеродинамічні поверхні, `UKeyPointComponent` — додаються у Blueprint-нащадку
  (напр. `Content/Airplanes/Cessna_172`).

### Властивості калібрування

`AAirplane::CalibrationSettings` (`FAircraftCalibrationSettings`,
`Structure/AircraftCalibrationSettings.h`):

- `ExpectedWingSpanMeters` — реальний розмах крил прототипу (м). Якщо `> 0`, у
  `BeginPlay` актор рівномірно масштабується так, щоб «дизайнерський» розмах
  (`UFlightDynamicsComponent::GetDesignWingSpanCm`, сира сума `|Offset.Y|` по
  `SurfaceForm[0]`) дорівнював цьому значенню.
- `ExpectedCenterOfMass` — заготовка, поки не використовується.

### Blueprint-класи віджетів

- `CameraWidgetClass` — інстанціюється, коли камера активна для цього літака (для
  локального pawn — на його PC, для цілі — на PC першого гравця, щоб трекер бачив
  фід камери цілі). Якщо це `UCameraViewWidget` — йому передається `SetAirplane(this)`.
- `TelemetryWidgetClass` — HUD-віджет лише на локально керованому pawn. Якщо це
  `UAirplaneTelemetryWidget` — `SetAirplane(this)`.

### Ініціалізація в дві стадії

- **`OnConstruction`** (в редакторі при розміщенні / зміні трансформу):
  `FlightDynamics->UpdateEditorVisualization(Mesh)` — оновлює редакторні оверлеї
  поверхонь, ініціалізує CoM, малює мітки точки тяги.
- **`BeginPlay`** (рантайм, фізика активна):
  1. Авто-калібрування масштабу за `CalibrationSettings`.
  2. Встановлює **порядок тіку** (див. нижче) через prerequisites.
  3. Підписується на делегати `UUAVSimulationSubsystem`:
     `OnVisualSettingsChanged` і `OnCameraSettingsChanged` → `RefreshConfigurations()`;
     `OnSensorSettingsChanged` → `RefreshSensorSettings()`. Одразу викликає обидва.

Код, що читає швидкість/кутову швидкість, має виконуватись у `BeginPlay` або пізніше.

### `RefreshConfigurations()`

Визначає роль літака за тегами актора (`Player` / `Target` / `AutoTracker`),
звіряє з прапорцями підсистеми і:

- вмикає/вимикає Niagara на кожній `UAerodynamicSurfaceSC` (`SetNiagaraActive`);
- створює (за потреби) і вмикає/вимикає `UUAVCameraComponent`
  (`SetCameraProcessingEnabled`) залежно від `Subsystem->OnboardCameraMode`;
- створює/знищує `CameraWidget` і `TelemetryWidget`.

Роль → режим: `EOnboardTargetMode::Drone` активний для `Player` **або**
`AutoTracker`; `Target` — лише для тегу `Target`; `None` — ніколи.

### `RefreshSensorSettings()`

Так само звіряє роль із `Subsystem->SensorsMode`, тоді для кожного сенсорного
компонента на акторі виставляє `bSensorEnabled = bSensorsActive && <прапорець типу>`
(перелік прапорців — у `04-SensorBus.md`).

### `SetupPlayerInputComponent`

Ітерує компоненти власника, для кожного, що реалізує `IPilotInputSource`, викликає
`BindInput(PlayerInputComponent)`. Актор агностичний до пристроїв — так само, як
`USensorBusComponent` агностичний до сенсорів.

### Телеметрійні геттери

- `GetAirspeedMs()` / `GetAirspeedKmh()` — модуль швидкості фізичного тіла
  фюзеляжу (через `FlightDynamics->GetAirspeed()`). **Не** `Actor->GetVelocity()` —
  корінь актора не симулює фізику.

## Порядок тіку за кадр

Встановлюється в `AAirplane::BeginPlay` через prerequisites, плюс
`UAttitudeControlComponent::ActivateAutopilot()` додає prerequisite на власний
actor:

```
AAirplane::Tick
  └─> UAttitudeControlComponent::TickComponent   (ZMQ PULL + PID — ЛИШЕ обчислення, кеш у Last*)
        └─> UPilotInputComponent::TickComponent   (зведення джерел + ЄДИНИЙ запис у FlightDynamics)
              └─> UFlightDynamicsComponent::TickComponent  (споживання ControlState; обнуляє його в кінці власного тіку)
```

Потрібно, бо `UFlightDynamicsComponent` скидає `ControlState` наприкінці свого
тіку. `UAeroVisualizerComponent` тікає в `TG_PostPhysics` з prerequisite на
`UFlightDynamicsComponent`.

## `UUAVSimulationSubsystem`

`Subsystem/UAVSimulationSubsystem.h` — `UWorldSubsystem`. Тримає рантайм-стан
конфігурації і три мультикаст-делегати:

| Поле | Тип | Призначення |
|------|-----|-------------|
| `CurrentSimulatorMode` | `ESimulatorMode` | Поточний режим (читає `USensorBusComponent::IsEnabledSensors`) |
| `bEnableVisualsForPlayer` / `bEnableVisualsForTarget` | `bool` | Чи вмикати Niagara-вихори для ролі |
| `OnboardCameraMode` | `EOnboardTargetMode` | На якій ролі активна бортова камера |
| `SensorsMode` | `EOnboardTargetMode` | На якій ролі активна сенсорна шина |
| `bEnableSensor*` (12 прапорців) | `bool` | Індивідуальні перемикачі типів сенсорів |
| `EWZones` | `TArray<TWeakObjectPtr<AEWZoneActor>>` | Усі зони РЕБ на сцені (слабкі вказівники, без кешу позиції/радіуса) |
| `WindVectors` | `TArray<TWeakObjectPtr<AWindActor>>` | Усі вітрові вектори на сцені (те саме) |
| `OnVisualSettingsChanged` | делегат | Broadcast зі `SetVisualSettings()` |
| `OnCameraSettingsChanged` | делегат | Broadcast зі `SetOnboardCameraMode()` |
| `OnSensorSettingsChanged` | делегат | Broadcast зі `SetSensorSettings()` |
| `OnEWSettingsChanged` | делегат | Broadcast зі `SetEWSettings()` |
| `OnWindSettingsChanged` | делегат | Broadcast зі `SetWindSettings()` |

Сеттери (`SetVisualSettings`, `SetOnboardCameraMode`, `SetSensorSettings`,
`SetEWSettings`, `SetWindSettings`) оновлюють поля й одразу роблять broadcast.
Кожен `AAirplane` підписаний у `BeginPlay`, тож broadcast має відбуватись
**після** спавну й опанування всіх акторів.

`GetWindVelocityAtLocation(WorldLocation) → FVector` (см/с) — векторна сума
внеску всіх `WindVectors` у заданій точці (не максимум, як у РЕБ — вітер
фізична швидкість, поля мають складатися). Використовується
`USubAerodynamicSurfaceSC` для впливу вітру на аеродинаміку — див.
`13-Environment-Actors.md` і `02-FlightDynamics.md`.

12 прапорців сенсорів: `Altimeter`, `AttitudeIndicator`, `CameraInclination`,
`Lidar`, `CameraFrame`, `CameraAltitude`, `SegmentationMask`, `BBoxDetection`,
`Position`, `GeoPosition`, `CesiumSurroundings`, `CustomSurroundings`.

## `AUAVSimulatorGameModeBase`

`UAVSimulatorGameModeBase.h/.cpp`. Редирект старого імені
`TP_BlankGameModeBase` → `UAVSimulatorGameModeBase` (`DefaultEngine.ini`).

### EditAnywhere-конфіг

- `CurrentSimulatorMode : ESimulatorMode`
- `TargetAirplaneClass`, `TrackerAirplaneClass : TSubclassOf<AAirplane>`
- `ScenarioSlotName : FString` (слот `USaveGame` сценарію; за замовч.
  `"TargetScenario_1"`)
- `TargetSpawnOffsetDistance : float` (см) — на скільки зсунути відтворювану
  траєкторію цілі вперед відносно старту трекера
- `AttitudeCommandEndpoint : FString` — ZMQ PULL endpoint для команд атитюду
  (`"tcp://*:5556"`)
- VFX: `bEnableVisualsForPlayer`, `bEnableVisualsForTarget`
- Camera: `OnboardCameraMode`
- Sensors: `SensorsMode` + 12 `bEnableSensor*`
- Global: `SensorWarmupFrameCount` (кількість кадрів прогріву сенсорів;
  сама логіка прогріву ще не реалізована)

### Потік

- `BeginPlay()` — штовхає всі прапорці в підсистему (щоб UI мав готовий стан),
  **і** одразу викликає `UpdateEWSettings()` + `UpdateWindSettings()` — обидва
  сканують рівень (`GetAllActorsOfClass`) і наповнюють `EWZones`/`WindVectors`
  підсистеми вже наявними в рівні зонами/векторами (`AEnvironmentActorManager`
  сам підсистему ніколи не чіпає, див. `13-Environment-Actors.md`). **Літаки
  (`AAirplane`) при цьому не спавнить.**
- `StartSimulation()` (BlueprintCallable, викликається з меню) — повторно штовхає
  налаштування (могли змінити в UI), спавнить актори за `CurrentSimulatorMode`,
  наприкінці робить `UpdateCameraSettings()` + `UpdateVisualSettings()` +
  `UpdateSensorSettings()` + `UpdateEWSettings()` + `UpdateWindSettings()`
  (broadcast після спавну).
- `StopSimulation()` — знищує всі `AAirplane`, кожному спершу `CleanupWidgets()`.
- `UpdateVisualSettings()` / `UpdateCameraSettings()` / `UpdateSensorSettings()` —
  штовхають відповідну групу EditAnywhere-полів у підсистему через її сеттери
  (з broadcast).
- `UpdateEWSettings()` / `UpdateWindSettings()` — без власних EditAnywhere-полів:
  свіжий `GetAllActorsOfClass(AEWZoneActor::StaticClass())` /
  `GetAllActorsOfClass(AWindActor::StaticClass())` по всьому рівні (бачить і
  вручну розміщені актори, і спавнені `AEnvironmentActorManager`) →
  `Subsystem->SetEWSettings(...)` / `SetWindSettings(...)`.

## Об'єкти середовища (РЕБ, вітер)

`AEnvironmentActorManager` (`Actor/EnvironmentActorManager.h/.cpp`) — окрема
сцена-актор, що спавнить і персистить `AEWZoneActor` (зони перешкод РЕБ) і
`AWindActor` (вітрові вектори, реально впливають на аеродинаміку через
`UUAVSimulationSubsystem::GetWindVelocityAtLocation`). Налаштовується вручну в
редакторі або через зовнішній Python-інструмент карти
(`Tools/ProjectTools/configurate_env_actors.py`, кнопка `ConfigurateEnvActorsBtn`
у `UEnvironmentSectionWidget`). Ніколи не торкається `UUAVSimulationSubsystem`
сам — це робить `AUAVSimulatorGameModeBase::UpdateEWSettings()`/
`UpdateWindSettings()` (вище). Детально — `13-Environment-Actors.md`.

## Режими симуляції (`ESimulatorMode`)

`Entity/SimulatorMode.h`. Спавн-логіка — у `StartSimulation()`. `PlayerStart`
задає стартовий трансформ. Актори спавняться `SpawnActorDeferred` +
`FinishSpawning`, з тегом ролі.

| Режим | Що спавнить | Теги | Особливе |
|-------|-------------|------|----------|
| `RecordTarget` | 1× `TargetAirplaneClass` | `Player` | Динамічно додає `UFlightRecorderComponent` (`SaveSlotName = ScenarioSlotName`), `StartRecording()` |
| `PlaybackAndTrack` | ціль + трекер | ціль `Target`, трекер `Player` | Ціль отримує `UFlightPlaybackComponent` (offset = `InitialRotation.Vector() * TargetSpawnOffsetDistance`); PC опановує трекер |
| `PlaybackAndAutoTrack` | ціль + трекер | ціль `Target`, трекер `AutoTracker` | Як вище, але на трекері `AttitudeControl->CommandEndpoint = AttitudeCommandEndpoint; ActivateAutopilot()` |
| `AutoTrack` | лише трекер на `PlayerStart` | `AutoTracker` | Без цілі й без відтворення; автопілот активовано, PC опановує трекер |
| `Playback` | лише ціль | `Target` | `UFlightPlaybackComponent` без offset; PC нікого не опановує |
| `Free` | 1× `TargetAirplaneClass` | `Player` | PC опановує; вільний ручний політ |

Завантаження сценарію: `UGameplayStatics::LoadGameFromSlot(ScenarioSlotName, 0)` →
`UFlightScenarioSave`. Якщо `FlightFrames` порожній — режим тихо не спавнить нічого.

## Ролі літаків (теги актора)

- **`Player`** — локально керований гравцем літак (детект також через
  `IsLocallyControlled()`).
- **`Target`** — ціль; має `UFlightPlaybackComponent`.
- **`AutoTracker`** — трекер під автопілотом; для перемикачів камери/сенсорів
  рахується як `Drone` (див. `IsRoleActiveForOnboardMode` в `Airplane.cpp`).

## `AUAVSimulatorPlayerController`

`UAVSimulatorPlayerController.h/.cpp`:

- `MenuWidgetClass : TSubclassOf<USimulatorMenuWidget>` — створюється в
  `BeginPlay`, `AddToViewport`, режим вводу `GameAndUI`, курсор увімкнено.
- `ShowMenu` / `HideMenu` / `ToggleMenu` / `IsMenuVisible`.
- Клавіші `Q` та `Gamepad_Special_Right` (Start на SN30 Pro+) →
  `OnQPressed()`: прибирає віджети камери, викликає `GameMode->StopSimulation()`,
  показує меню, відкриває секцію `Scenario`.
- `RegisterCameraWidget` / `RemoveCameraWidgets` — `AAirplane` реєструє
  створений віджет камери, щоб PC міг його прибрати.

## Сумісні псевдоніми (compatibility aliases)

У кількох entity-заголовках залишені `using`-аліаси на час міграції:
`AerodynamicForce` → `FAerodynamicForce`, `ControlInputState` → `FControlInputState`,
`PolarRow` → `FPolarRow`. Нові виклики мають використовувати `F`-форми.
