# 09 — Генерація синтетичних датасетів

Актори-інструменти під `Source/UAVSimulator/DatasetGen/`. Розміщуються в рівні,
конфігуруються в Details-панелі, запускаються кнопкою `CallInEditor` або з секції
`SyntheticData` меню. Не є частиною рантайм-симуляції.

## `ADroneDatasetGeneratorActor` — силует дрона («Spherical Contour»)

`DroneDatasetGeneratorActor.h/.cpp`. `Blueprintable`.

Орбітує `USceneCaptureComponent2D` навколо спавненого Blueprint дрона по сфері
(азимут/елевація), рендерить із **show-only list** (лише дрон на гарантовано
чорному тлі — без залежності від post-process матеріалу; `MaskPostProcessMaterial`
опційно інжектиться зверху), витягує силуетний полігон через OpenCV.

**Конфіг:**
- Setup: `DroneBlueprintClass`, `MaskPostProcessMaterial` (опц.).
- Camera: `CameraFOV` (60), `RenderWidth`/`RenderHeight` (640×480).
- Sweep: `OrbitRadius` (2000 см), `AzimuthStep` (45°), `ElevationStep` (30°;
  кільця на −90+step … 90−step, полюси завжди включені окремими кадрами).
- Processing (OpenCV): `SilhouetteBlurSize` (7, гаусів блюр перед порогом),
  `FillGapsSize` (15, morph-close — зливає шасі/антени в тіло), `RemoveIslandsSize`
  (9, morph-open — стирає ізольовані блоби), `PolygonSmoothness` (0.02,
  `approxPolyDP` як частка периметру).
- Output: `OutputJsonPath`, `bSaveDebugImages` (true), `OutputImageDir` (авто
  `<json>_frames/`).

**Крок для кадру:** спавн дрона + позиція камери на орбіті → тимчасово
show-only-drone + `CaptureScene()` + `ReadPixels()` → OpenCV (Otsu-поріг +
morph-close + `approxPolyDP`) → накопичення в JSON + опційні debug-PNG.
`GenerateDataset()` **блокує редактор** до завершення.

## `ADroneKeyPointDatasetActor` — ключові точки

`DroneKeyPointDatasetActor.h/.cpp`. `Blueprintable`.

Спавнить дрон (має містити `UKeyPointComponent`-и в Blueprint), читає локальну
позицію кожного `UKeyPointComponent`, експортує **безрозмірні** (нормалізовані)
3D-координати: усі ділиться на макс. абсолютну координату → кожна компонента в
`[-1, 1]`. Сирий масштаб (см) зберігається в JSON для відновлення.

**Конфіг:** `DroneBlueprintClass`, `OutputJsonPath`. Кнопка `ExportKeyPoints()`.

**JSON:**
```json
{"drone_model": "MyDrone_C", "scale_cm": 245.3,
 "keypoints": [{"id": "nose", "x": 0.501, "y": 0.0, "z": 0.12}, ...]}
```

## `ASceneObjectDatasetActor` — об'єкти сцени

`SceneObjectDatasetActor.h/.cpp`. `Blueprintable`. Редактор/рантайм.

Сканує **всіх** акторів світу, експортує світову позицію та розмір AABB (Unreal
units). Пропускає: `AAirplane` (гравець/ціль — за перевіркою класу, без тегів),
актори з класом у `ExcludedActorClassNames` (проти `GetClass()->GetName()`),
акторів без `UStaticMeshComponent`.

**Конфіг:** `OutputJsonPath` (`"C:/Datasets/scene_objects.json"`),
`ExcludedActorClassNames : TArray<FString>`. Кнопка `ExportSceneObjects()`.

**JSON:**
```json
{"objects": [{"name": "Building_1", "class": "StaticMeshActor",
              "x": 120.0, "y": -50.0, "z": 0.0,
              "size_x": 200.0, "size_y": 150.0, "size_z": 300.0}]}
```

## `AYoloMarkerDatasetActor` — YOLO-датасет маркерів мапи

`YoloMarkerDatasetActor.h/.cpp`. `Blueprintable`. **Не** блокуючий цикл — стейт-машина
на `Tick`; вимагає **запущеного (Play) світу** (потрібен живий стрімінг Cesium),
запускається з секції `SyntheticData`.

Аналог `ADroneDatasetGeneratorActor` для маркерів мапи: замість орбіти дрона й
силуету — орбітує `USceneCaptureComponent2D` навколо **кожного** маркера з
JSON-джерела `UCustomSurroundingsScannerComponent` і пише YOLO detection-датасет:
один RGB-кадр на позу камери + піксельний bbox **кожного** маркера, видимого в
цьому кадрі.

**Джерело маркерів:** та сама схема, що
`UCustomSurroundingsScannerComponent::ObjectsJson` (`{elementId, type, altitude,
bbox:{x_min,x_max,y_min,y_max}}`). `ObjectsSourceFilePath` (за замовч.
`Tools/TestingPlatform/attitude_control/marker/map_objects.json`, з fallback на
legacy `.../attitude_control/map_objects.json`); `ObjectsJsonInline` як запасний.
Кожен кут → світ через `ACesiumGeoreference` + snap на рельєф
(`ResolveGroundHeights`, як у сканера — JSON `altitude` не використовується для
розміщення).

**Ключові групи конфігу:**
- Source: `ObjectsSourceFilePath`, `ObjectsJsonInline`, `OnlyMarkerId`, `MaxMarkers`.
- Camera: `RenderWidth`/`RenderHeight` (1280×720), `CameraFOV` (70).
- Sweep: `OrbitRadiiMeters : TArray<float>` (одна повна сфера азимут/елевація на
  запис), `AzimuthStep` (45°), `ElevationMinDeg` (20), `ElevationMaxDeg` (70),
  `ElevationStep` (25), `AimJitterDeg` (4° — ціль не завжди в центрі),
  `MarkerHeightMeters` (екструзія footprint-квада вгору), `DomeLiftMeters` (5),
  `CameraGroundClearanceMeters` (15 — камера не занурюється під рельєф).
- Filter: `MinBBoxPixels` (12), `MinVisibleFraction` (0.5 — бокс кліпиться до
  кадру), `bRequireTargetVisible` (true), `bRequireLineOfSight` (true),
  `LineOfSightToleranceMeters` (3), `MaxLabelDistanceMeters` (0 = без ліміту).
- Ground: `bSnapMarkersToTileSurface`, `GroundTraceChannel`, `GroundTraceSpanMeters`
  (2000), `MaxGroundResolvesPerTick` (6), `MaxGroundResolveAttempts` (16).
- Timing: `SettleFrames` (4), `MaxSettleFrames` (90), `TileLoadProgressTarget`
  (97%), `TileReadyHoldFrames` (2).
- Tiles: `CulledTileScreenSpaceError` (128 — на час sweep-у кожен `ACesium3DTileset`
  примусово в seamless-стан: `ForbidHoles`, fog/frustum culling off, out-of-view
  тайли пришпилені до грубого SSE; усі оригінальні прапорці відновлюються),
  `CesiumFrustumMargin` (1.25).
- Output: `OutputRootDir` (`images/{train,val}/`, `labels/{train,val}/`,
  `data.yaml`, `dataset.json`, опц. `debug/`), `ValSplit` (0.15), `bSaveDebugImages`
  (false), `bSaveAsJpeg` (true).

**Стейт-машина за позу:** перемістити камеру → чекати `SettleFrames` тіків (+ поки
`MinTilesetLoadProgress` не досягне `TileLoadProgressTarget`, з утриманням
`TileReadyHoldFrames`) → `CaptureScene()` + `ReadPixels` → проєкція + збереження
(`SaveFrame` пише кадр + `.txt` YOLO-мітки) → наступна.

Кнопки: `GenerateDataset()` / `CancelGeneration()` (флашить зроблене).
Також пише `exp_geo_position.txt` (`<frame> <lat> <lon> <alt_m>`), `classes.json`,
`virtual_map.json` (id → lat/lon кожного маркера).

## Прив'язка до UI

Секція `SyntheticData` меню (`USyntheticDataSectionWidget`) знаходить ці актори в
рівні, тримає їхні шляхи виводу в `USyntheticDataSettingsSave` і запускає їх
кнопками. `RunMarkerDatasetBtn` / `MarkerDatasetPathTextBox` — `BindWidgetOptional`.
