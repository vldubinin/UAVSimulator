# 08 — Меню симулятора та налаштування

> **Мова інтерфейсу — українська.** Усі підписи `TextBlock` у `Content/UI/**`
> (`WBP_SimulatorMenu`, секції, `WBP_AirplaneTelemetry`) переведено українською.
> Технічні ідентифікатори (`Топік: camera_tp`, `ISO`, `EV`, `BBox`) лишено як є.
> Рядки опцій `ComboBoxString` додаються з C++ (`ModeToString`,
> `OnboardTargetModeToString`, `StreetLightsDataSourceToString`), а не з UMG, тому
> вони поки англійські (`Free`, `Drone`, `Custom`, `Cesium` …).

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
`OriginLongitude`, `OriginHeight`, `TimeZone`, `SolarTime`, `StarsDensity/Threshold/
PointSize/Intensity`, `CloudsCoverage/Density/Speed`, `bTerrainSurfaceEnabled`,
`RainIntensity`, `StreetLightsBrightness`, `StreetLightsDataSource`).

- `SpinBoxOriginLatitude/Longitude/Height` — `ACesiumGeoreference`.
- `SpinBoxTimeZone/SolarTime` — `ACesiumSunSky`.
- `SpinBoxStarsDensity/Threshold/PointSize/Intensity` — параметри матеріалу
  `MI_Stars` на `StarsSphere` всередині `CesiumSunSky` (`UpdateNightVisuals`):
  разом із яскравістю зірок (`NightFactor` від нахилу сонця) дзеркалить "місяць" —
  другий `UDirectionalLightComponent` (`AtmosphereSunLightIndex == 1`) — проти сонця.
  Сам матеріал — процедурне 3D-поле зір (`M_Stars`); алгоритм, параметри й виправлення
  «кіл» при високій щільності — у `13-Environment-Actors.md` (розділ «Нічне небо: зорі»).
- `SpinBoxCloudsCoverage/Density/Speed` — параметри MID хмарового матеріалу
  `VolumetricCloud` (створюється лінькаво в `ApplyCloudsSettingsFromWidgets`).
- `TerrainSurfaceCB` — `ACesium3DTileset` вкл/викл; при вимкненому Cesium-рельєфі
  спавнить fallback `DefaultSkyboxClass` / `DefaultSunClass`
  (`ApplyTerrainSurfaceState`).
- `ConfigurateEnvActorsBtn` (`OptionalWidget = true`) → `OnConfigurateEnvActorsBtnClicked()`:
  знаходить наявний `AEnvironmentActorManager` у рівні (`GetEnvironmentActorManager()`,
  спавнить, якщо нема) і викликає `Manager->OpenConfigurationTool()` — синхронно
  відкриває Python-інструмент карти (`Tools/ProjectTools/configurate_env_actors.py`)
  для розміщення зон РЕБ і вітрових векторів. Детально — `13-Environment-Actors.md`.
- `SpinBoxRainIntensity` (`OptionalWidget = true`) → `OnRainIntensityCommitted()`:
  `Manager->SetRainIntensity(Value)` — єдине поле керування дощем (знаходить
  наявний `ARainEffectManager` у рівні через `GetRainEffectManager()`, спавнить,
  якщо нема): `0` вимикає дощ над усіма літаками повністю, `> 0` — значення прокидається в кожен активний `UNiagaraComponent` дощу як User Parameter `Intensity` (Float), яке в `NS_Rain` є безпосередньо Spawn Rate (частинок/с); діапазон спінбокса — 0–150000 (див. `12-Niagara.md`). Детально — `13-Environment-Actors.md`.
- `SpinBoxStreetLightsBrightness` (`OptionalWidget = true`) →
  `OnStreetLightsBrightnessCommitted()`: `Manager->SetBrightness(Value)`, діапазон
  `0..100` (`0` — вогні вимкнені, `100` — максимум). `GetStreetLightsManager()` лениво
  спавнить `AStreetLightsManager`, якщо його ще нема в рівні.
- `ComboBoxStreetLightsDataSource` (`UComboBoxString`, `OptionalWidget = true`) →
  `OnStreetLightsDataSourceChanged()`: `Manager->SetDataSource(EStreetLightsDataSource)`
  — джерело будівель для вогнів: `Custom` (`UCustomSurroundingsScannerComponent`) або
  `Cesium` (`UCesiumSurroundingsScannerComponent`). Опції додаються в `NativeConstruct`;
  підписка на `OnSelectionChanged` робиться **після** початкового Load/Sync, щоб
  програмний `SetSelectedOption` не тригерив зайве збереження. Перемикання скидає
  вже розставлені вогні. Детально — `13-Environment-Actors.md`.

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
| `UEnvironmentSettingsSave` | `EnvironmentSettings` | `OriginLatitude/Longitude/Height`, `TimeZone`, `SolarTime`, `StarsDensity/Threshold/PointSize/Intensity`, `CloudsCoverage/Density/Speed`, `bTerrainSurfaceEnabled`, `RainIntensity`, `StreetLightsBrightness` (дефолт 0), `StreetLightsDataSource` (дефолт `Custom`); залишкові `bEWInterferenceEnabled`, `EWLongitude/Latitude/Radius` — застарілі, не використовуються |
| `USyntheticDataSettingsSave` | `SyntheticDataSettings` | `SphericalContourBasePath`, `KeyPointOutputJsonPath`, `SceneObjectOutputJsonPath`, `MarkerDatasetBasePath`, `bEnableSensorSegmentationMask`, `bEnableSensorBBoxDetection` |
| `UGlobalSettingsSave` | `GlobalSettings` | `SensorWarmupFrameCount` |
| `UFlightScenarioSave` | `ScenarioSlotName` (за замовч. `TargetScenario_1`) | траєкторія — див. [07](07-Recording-Playback-Modes.md) |

## HUD-віджети (на pawn гравця)

### `UAirplaneTelemetryWidget`

`UI/AirplaneTelemetryWidget.h/.cpp`. HUD-readout. `SetAirplane(AAirplane*)` (той
самий патерн, що `UCameraViewWidget`). `AAirplane` створює його з
`TelemetryWidgetClass` лише для локально керованого pawn.

**Значення оновлює C++, а не Blueprint.** `NativeTick` щокадру форматує тексти в
опційні `BindWidgetOptional`-`TextBlock`-и з `WBP_AirplaneTelemetry` (відсутній —
просто пропускається):

| Віджет (Is Variable) | Формат | Джерело |
|----------------------|--------|---------|
| `SpeedValueText` | `123 км/год` | `GetAirspeedKmh()` → `AAirplane::GetAirspeedKmh()` (швидкість фізичного тіла) |
| `AltitudeValueText` | `350 м` | `GetAltitudeMeters()` — **геодезична висота** з `ACesiumGeoreference` (та сама конвертація Unreal → Georeference-local → LLH, що в `UGeoPositionDroneComponent`); резерв без Georeference — Z актора |
| `ThrottleValueText` | `65 %` | `GetThrottlePercent()` = `AAirplane::GetThrottle01() * 100` — **фактичний** дросель (`CurrentThrottle` після інерції розкручування), не команда пілота |
| `ThrustValueText` | `9750 Н` | `GetThrustN()` → `UFlightDynamicsComponent::CurrentThrustN` |
| `PitchValueText` | `-2.5°` | `GetPitchDeg()` — `GetActorRotation().Pitch` (та сама, що в `UAttitudeIndicatorComponent`) |
| `RollValueText` | `12.0°` | `GetRollDeg()` — `GetActorRotation().Roll` |

Усі геттери лишаються `BlueprintPure` (`GetAltitudeMeters`, `GetAirspeedMs`,
`GetAirspeedKmh`, `GetThrottlePercent`, `GetThrustN`, `GetPitchDeg`, `GetRollDeg`).
Раніше значення у WBP були статичним «0.0» без будь-якої логіки.

**`WBP_AirplaneTelemetry`:** один `HorizontalBox` внизу екрана — пари «підпис /
значення» (Швидкість, Висота, Тангаж, Крен, Дросель, Тяга), кожен елемент у своєму
`VerticalBox`, між ними `Spacer`-и (15 між підписом і значенням, 50 між парами).
`BindWidgetOptional` вимагає, щоб `TextBlock`-и значень мали **точно ці імена** і були
позначені Is Variable; імена не можна дублювати в C++ звичайними `UPROPERTY` — UMG-
компілятор падає з "another object already exists". Додаючи нове поле: `TextBlock` +
`BindWidgetOptional` член у `UAirplaneTelemetryWidget` + рядок у `NativeTick`.

### `UCameraViewWidget`

`UI/CameraViewWidget.h/.cpp`. Тримає `AAirplane*` (`SetAirplane`). Бінд текстури
до `GetCameraOutputTexture()` — у UMG-Blueprint.
