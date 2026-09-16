# 08 — Меню симулятора та налаштування

## `AUAVSimulatorPlayerController`

`UAVSimulatorPlayerController.h/.cpp`. Див. також
[01-Architecture.md](01-Architecture.md#auavsimulatorplayercontroller).

- `MenuWidgetClass : TSubclassOf<USimulatorMenuWidget>` — створюється в
  `BeginPlay`, `AddToViewport`, режим вводу `GameAndUI`, курсор увімкнено.
- `ShowMenu` / `HideMenu` / `ToggleMenu` / `IsMenuVisible`.
- `SetupInputComponent` біндить `Q` та `Gamepad_Special_Right` (Start на SN30
  Pro+) на `OnQPressed()`: `RemoveCameraWidgets()` → `GameMode->StopSimulation()`
  → `ShowMenu()` → `MenuWidget->OpenSection(EMenuSection::Scenario)`.
- `RegisterCameraWidget` / `RemoveCameraWidgets` — `AAirplane` реєструє
  створений віджет камери, щоб PC прибрав його при виході в меню.

## `USimulatorMenuWidget`

`UI/SimulatorMenuWidget.h/.cpp`. Верхньорівневе UMG-меню.

**BindWidget (мають існувати з цими іменами в Blueprint):**
- Кнопки навігації: `ButtonScenario`, `ButtonSensors`, `ButtonEnvironment`,
  `ButtonSyntheticData`, `ButtonGlobal`, `ButtonStartSimulation`.
- `ContentSwitcher : UWidgetSwitcher` — діти в порядку: `0=Scenario`, `1=Sensors`,
  `2=Environment`, `3=SyntheticData`, `4=Global`.

**`OpenSection(EMenuSection)`** (`BlueprintCallable`) — `NotifySectionChange` →
`ContentSwitcher->SetActiveWidgetIndex((int32)Section)`. `NotifySectionChange`
викликає `OnSectionDeactivated()` на старій панелі й `OnSectionActivated()` на
новій (якщо вони — `USimulatorSectionWidget`).

`NativeConstruct` біндить кнопки й одразу відкриває `Scenario`.
`OnStartSimulationClicked` → `GetGameMode()->StartSimulation()`.

`EMenuSection` (`Entity/MenuSection.h`): `Scenario`, `Sensors`, `Environment`,
`SyntheticData`, `Global`. `DronesSectionWidget` існує як тип, але в перелік
не входить (контент лише в Blueprint).

## Базовий клас секції — `USimulatorSectionWidget`

`UI/Sections/SimulatorSectionWidget.h`. `Abstract`. Два
`BlueprintNativeEvent`-и: `OnSectionActivated()` / `OnSectionDeactivated()`
(за замовч. пусті `_Implementation`).

## Секції

Кожна секція в `OnSectionActivated` синхронізується з GameMode / світом і завантажує
збережені налаштування; на зміну поля — пише в GameMode і в `USaveGame`.

### `UScenarioSectionWidget`

Слот `ScenarioSettings` → `UScenarioSettingsSave`.

- `ComboBoxMode` — вибір `ESimulatorMode` (`ModeToString`/`StringToMode`).
- `EditableTextTrajectoryName` / `TrajectoryName` — `ScenarioSlotName` (ім'я
  траєкторії; ховається для `RecordTarget` — `RefreshTrajectoryNameVisibility`).
- `PanelOffsetDistance` / `SpinBoxOffsetDistance` / `TargetOffsetDistanceText` —
  `TargetSpawnOffsetDistance`; панель ховається для `RecordTarget`
  (`RefreshOffsetVisibility`).
- `ComboBoxOnboardCameraMode` — `OnboardCameraMode` (`EOnboardTargetMode`:
  Drone / Target / None).
- `ComboBoxSensorsMode` — `SensorsMode`.

### `USensorsSectionWidget`

Слот `SensorSettings` → `USensorSettingsSave`. `UCheckBox` на кожен сенсор:
`CameraFrameCB`, `CameraAltitudeCB`, `AltimeterCB`, `CameraInclinationCB`,
`LidarCB`, `PositionCB` — обов'язкові; `AttitudeIndicatorCB`, `GeoPositionCB`,
`CesiumSurroundingsCB`, `CustomSurroundingsCB` — `OptionalWidget = true` (можна не
додавати в UMG-Blueprint, код це терпить). Кожен `On*Changed` пише відповідний
`GameMode->bEnableSensor*` і зберігає.

### `UEnvironmentSectionWidget`

Слот `EnvironmentSettings` → `UEnvironmentSettingsSave` (`OriginLatitude`,
`OriginLongitude`, `OriginHeight`, `TimeZone`, `SolarTime`, `bTerrainSurfaceEnabled`).

- `SpinBoxOriginLatitude/Longitude/Height` — `ACesiumGeoreference`.
- `SpinBoxTimeZone/SolarTime` — `ACesiumSunSky`.
- `TerrainSurfaceCB` — `ACesium3DTileset` вкл/викл; при вимкненому Cesium-рельєфі
  спавнить fallback `DefaultSkyboxClass` / `DefaultSunClass`
  (`ApplyTerrainSurfaceState`).
- `ConfigurateEnvActorsBtn` (`OptionalWidget = true`) → `OnConfigurateEnvActorsBtnClicked()`:
  знаходить наявний `AEnvironmentActorManager` у рівні (`GetEnvironmentActorManager()`,
  спавнить, якщо нема) і викликає `Manager->OpenConfigurationTool()` — синхронно
  відкриває Python-інструмент карти (`Tools/ProjectTools/configurate_env_actors.py`)
  для розміщення зон РЕБ і вітрових векторів. Детально — `13-Environment-Actors.md`.

### `USyntheticDataSectionWidget`

Слот `SyntheticDataSettings` → `USyntheticDataSettingsSave`
(`SphericalContourBasePath`, `KeyPointOutputJsonPath`, `SceneObjectOutputJsonPath`,
`MarkerDatasetBasePath`, `bEnableSensorSegmentationMask`, `bEnableSensorBBoxDetection`).

Кнопки + текст-бокси шляхів для інструментів `DatasetGen/` (див.
[09-Dataset-Generation.md](09-Dataset-Generation.md)):
- `RunSphericalContourBtn` / `SphericalContourFilePathTextBox` →
  `ADroneDatasetGeneratorActor`.
- `RunKPointDetectionBtn` / `KPointDetectionBtnTextBox` → `ADroneKeyPointDatasetActor`.
- `RunSceneObjectExportBtn` / `SceneObjectExportPathTextBox` → `ASceneObjectDatasetActor`.
- `RunMarkerDatasetBtn` / `MarkerDatasetPathTextBox` (`BindWidgetOptional`) →
  `AYoloMarkerDatasetActor`.
- `SegmentationMaskCB`, `BBoxDetectionCB` — прапорці сенсорів.

### `UGlobalSectionWidget`

Слот `GlobalSettings` → `UGlobalSettingsSave` (`SensorWarmupFrameCount`).
`SpinBoxSensorWarmupFrames` → `GameMode->SensorWarmupFrameCount` (кількість кадрів
прогріву сенсорів; сама логіка прогріву ще не реалізована — секція лише експонує
значення).

## Save-об'єкти налаштувань

| Клас (`Save/`) | Слот | Поля |
|----------------|------|------|
| `UScenarioSettingsSave` | `ScenarioSettings` | `CurrentSimulatorMode`, `ScenarioSlotName`, `TargetSpawnOffsetDistance`, `OnboardCameraMode`, `SensorsMode` |
| `USensorSettingsSave` | `SensorSettings` | `bEnableSensorCameraFrame`, `…Altimeter`, `…AttitudeIndicator`, `…CameraInclination`, `…Lidar`, `…CameraAltitude`, `…Position`, `…GeoPosition`, `…CesiumSurroundings`, `…CustomSurroundings` |
| `UEnvironmentSettingsSave` | `EnvironmentSettings` | `OriginLatitude/Longitude/Height`, `TimeZone`, `SolarTime`, `bTerrainSurfaceEnabled` |
| `USyntheticDataSettingsSave` | `SyntheticDataSettings` | `SphericalContourBasePath`, `KeyPointOutputJsonPath`, `SceneObjectOutputJsonPath`, `MarkerDatasetBasePath`, `bEnableSensorSegmentationMask`, `bEnableSensorBBoxDetection` |
| `UGlobalSettingsSave` | `GlobalSettings` | `SensorWarmupFrameCount` |
| `UFlightScenarioSave` | `ScenarioSlotName` (за замовч. `TargetScenario_1`) | траєкторія — див. [07](07-Recording-Playback-Modes.md) |

## HUD-віджети (на pawn гравця)

### `UAirplaneTelemetryWidget`

`UI/AirplaneTelemetryWidget.h/.cpp`. Тонкий readout. `SetAirplane(AAirplane*)`
(той самий патерн, що `UCameraViewWidget`). `BlueprintPure`-геттери для біндингу
`TextBlock`-ів у Blueprint: `GetAltitudeMeters()`, `GetAirspeedMs()`,
`GetAirspeedKmh()`, `GetPitchDeg()`, `GetRollDeg()`. Верстка — у Blueprint.
`AAirplane` створює його з `TelemetryWidgetClass` лише для локально керованого pawn.

### `UCameraViewWidget`

`UI/CameraViewWidget.h/.cpp`. Тримає `AAirplane*` (`SetAirplane`). Бінд текстури
до `GetCameraOutputTexture()` — у UMG-Blueprint.
