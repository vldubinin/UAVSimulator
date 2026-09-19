# 05 — Сканери оточення

Два сенсори з однаковою формою (обидва `IUAVSensorInterface`, обидва проєктують
об'єкти на кадр бортової камери), але з різними джерелами об'єктів:

- **`UCesiumSurroundingsScannerComponent`** — фізичний sweep по Cesium 3D Tiles,
  читає метадані фіч.
- **`UCustomSurroundingsScannerComponent`** — фіксований JSON-список об'єктів,
  прив'язаних до рельєфу.

Обидва тримають персистентне `ObjectStorage` (мапа поточно видимих об'єктів,
мутується **лише** через `AddObject()` / `RemoveObject()`), і все нижнє за
течією — `LatestScanResults`, консольний лог, debug-промені, JSON-payload —
береться з `ObjectStorage`, а не зі свіжого результату скану.

---

## `UCesiumSurroundingsScannerComponent`

`Components/CesiumSurroundingsScannerComponent.h/.cpp`. Топік `cesium_objects`.

### Принцип

Sweep сітки напрямків, що покриває **точно** горизонтальний/вертикальний FOV
бортової камери (`UUAVCameraComponent::HorizontalFOVDeg`/`VerticalFOVDeg`), із
трансформу `USceneCaptureComponent2D`. Кожен напрямок — це **товста сфера**
(`SweepMultiByChannel`, радіус `SweepRadius`), а не нульова лінія, щоб об'єкти між
кутово-сусідніми променями не прослизали на дальності. Для кожного хіту, що
влучив у фічу Cesium 3D Tiles з таблицею властивостей (налаштовується на
`CesiumFeaturesMetadataComponent` тайлсету), читає метадані через
`UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit`. Хіти в ту
саму фічу зливаються в один запис за скан.

### Звірка з `ObjectStorage`

1. Фіча вже в сховищі — лишається недоторканою, доки її продовжують хітати; її
   дані **заморожені** на момент першого виявлення.
2. Фіча, яку sweep цього скану не хітнув (миттєвий проміжок між променями або
   оклюзія), **не** видаляється одразу: її заморожена світова позиція
   ре-проєктується на камеру, і вона лишається в сховищі, доки проєкція в межах
   кадру. Видаляється, лише коли заморожена позиція випадає з кадру (або за камеру).
3. `LatestScanResults` дзеркалить `ObjectStorage`.

### Параметри скану

| Властивість | Замовч. | Роль |
|-------------|---------|------|
| `ScanRadiusMeters` | 10000 | Макс. дальність (м) |
| `HorizontalRays` | 72 | Променів на горизонтальний FOV |
| `VerticalLayers` | 8 | Променів на вертикальний FOV |
| `ScanRate` | 1.0 | Повних сканів на секунду |
| `FramesBeforeLowerHalfCutoff` | 10 | Після N викликів `Scan()` нижня половина вертикального FOV (усе під віссю вперед) назавжди виключається зі сітки. 0 — вимкнено |
| `CollisionChannel` | `ECC_Visibility` | Канал sweep-у |
| `SweepRadius` | 100 см | Радіус сфери вздовж кожного променя |
| `FeatureIDSetIndex` | 0 | Індекс набору Feature ID на примітиві |

### Sensor-вивід

`ObjectPropertyName` (`"elementId"`), `LatitudePropertyName` (`"cesium#latitude"`),
`LongitudePropertyName` (`"cesium#longitude"`), `AltitudePropertyName` (`"Height"`)
— ключі таблиці властивостей, з яких читаються id / lat / long / altitude
(конфігуровано, бо різні тайлсети іменують по-різному).

**Payload** (`BuildSensorFrame`, з `ObjectStorage`):
```json
{"objects": [
  {"id": "...", "latitude": <d>, "longitude": <d>, "altitude": <d>,
   "pixel_x": <d>, "pixel_y": <d>, "visible": <bool>}
]}
```
`pixel_x/pixel_y` = `-1` коли `visible=false`. Проєкція
(`ProjectWorldToScreen`) — та сама view/projection-математика, що в
`UKeyPointDetectionComponent`.

### Оптимізація broad-phase

`SweepScan` перед фізичними запитами звужує сітку: `GatherNearbyTileComponents`
(дешева перевірка bounding-sphere уже завантажених тайл-примітивів у межах
дальності) → `BuildActiveCellSet` (проєкція angular-footprint кандидатів у
H/V-кутовий простір сітки; sweep лише активних клітин). Кандидат, що перетинає
площину камери або за нею, консервативно активує всю сітку. Немає тайлсету →
sweep усієї сітки, як раніше.

### Debug

- `bDrawRayDebug` (true) — вмикає/вимикає відладочні промені; `RayDebugColor`
  (Yellow) — один промінь на відстежувану фічу, щотіку заново від поточної позиції
  літака. Вимкніть `bDrawRayDebug` на Blueprint літака (`CesiumSurroundingsScanner`),
  якщо промені заважають — вони не впливають ні на сканування, ні на
  `AStreetLightsManager`.
- `bDrawScanArea` (true), `ScanAreaDebugColor` (Cyan) — дротяний контур зони
  sweep-у (4 ребра з origin до дальніх кутів + дальній прямокутник); відображає
  live `FramesBeforeLowerHalfCutoff` (після відсічки нижнє ребро сідає на вісь
  вперед).

Debug і payload малюються/будуються лише при `bSensorEnabled`.
`UpdateSensorSize` (на початку кожного тіку, безумовно) ліниво читає розмір
render target — щоб retention-тест «ще в кадрі?» працював незалежно від того, чи
увімкнена ZMQ-публікація.

### `FCesiumSurroundingObject`

`Structure/CesiumSurroundingObject.h`: `ObjectID` (стабільний ключ
`BuildFeatureKey` = actor|component|відсортовані метадані), `ActorName`,
`ComponentName`, `DistanceMeters`, `HitLocationMeters` (світ, м), `Metadata`
(`TMap<FString,FString>`).

---

## `UCustomSurroundingsScannerComponent`

`Components/CustomSurroundingsScannerComponent.h/.cpp`. Топік `custom_objects`.

### Джерело об'єктів

`ObjectsJson` (EditAnywhere, MultiLine) — масив записів:
```json
{
  "elementId": "obj3", "type": "building", "altitude": 350,
  "bbox": {
    "x_min": {"latitude": ..., "longitude": ...},
    "x_max": {"latitude": ..., "longitude": ...},
    "y_min": {"latitude": ..., "longitude": ...},
    "y_max": {"latitude": ..., "longitude": ...}
  }
}
```
Хардкоджений дефолт (`DefaultCustomObjectsJson` у .cpp) — заготовка під майбутнє
файлове/мережеве джерело; схема збігається зі схемою
`Tools/TestingPlatform/attitude_control/marker/map_objects.json`.

**`LoadObjects()`** (BeginPlay + щоразу коли `ObjectsJson != LastLoadedObjectsJson`
— live-reload): кожен із 4 кутів `bbox` конвертується у світ на висоті 0 через
`ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal`
(`BBoxCornersWorldMeters`, winding `x_min→x_max→y_min→y_max`); центр footprint =
середнє чотирьох кутів (`Latitude`/`Longitude`/`WorldLocationMeters`). JSON
`altitude` **не** використовується для розміщення.

### Прив'язка до рельєфу

Кожен `Scan()`, поки `bSnapMarkersToTileSurface` і `!bGroundHeightResolved`:
`ResolveGroundHeights` робить вертикальний line-trace (`TryTraceTileSurfaceMeters`)
уздовж локальної «вертикалі» (`GeographicUpMeters` — нормаль еліпсоїда) від кожного
кута ±`GroundTraceSpanMeters`, приймає перший блокуючий хіт у `ACesium3DTileset`
(або будь-який, якщо `!bRequireCesiumTilesetHit`), піднімає на
`GroundHeightOffsetMeters`. Ретраїться, бо Cesium стрімить тайли за дистанцією
камери. `bGroundHeightResolved` стає `true`, коли всі кути влучили.

Параметри Ground: `bSnapMarkersToTileSurface` (true), `GroundTraceChannel`
(`ECC_Visibility`), `GroundTraceSpanMeters` (20000), `GroundHeightOffsetMeters`
(0), `bRequireCesiumTilesetHit` (true), `bDebugGroundTrace` (true).

### Скан

**`Scan()`**: для кожного `AllObjects` — (за потреби) `ResolveGroundHeights`;
`DistanceMeters`; `bVisible = inRange && ProjectWorldToScreen(centre)`. Видимі →
звірка з `ObjectStorage` (повний overwrite, щоб live-редагування JSON доходило й
до вже видимих). Немає фізичного sweep-у й тесту оклюзії — це точні точки, прямий
per-tick тест самодостатній.

Параметри скану: `ScanRadiusMeters` (10000).

### Sensor-вивід

**Payload** (`BuildSensorFrame`, з `ObjectStorage`):
```json
{"objects": [
  {"id","type","latitude","longitude","altitude",
   "bboxw","bboxh","pixel_x","pixel_y",
   "corners_px": [x0,y0,x1,y1,x2,y2,x3,y3],
   "visible": <bool>}
]}
```
- `bboxw`/`bboxh` — pixel-space розмір спроєктованого footprint-квада
  (`ComputeBBoxScreenSize` над `BBoxCornersWorldMeters`, unclamped).
- `corners_px` — 4 кути footprint, спроєктовані окремо (плоский масив, winding
  `x_min→x_max→y_min→y_max`, unclamped; кут за камерою = `[-1,-1]`) — щоб споживач
  міг підігнати орієнтований (повернутий) бокс, не лише AABB.
- `pixel_x/pixel_y` = `-1` коли `visible=false`.

`ProjectWorldToScreen` = `ProjectWorldToScreenUnclamped` + перевірка меж;
математика ідентична `UCesiumSurroundingsScannerComponent`.

### Debug

`bDrawRayDebug` (вкл/викл променів) + `RayDebugColor` (колір променя до кожного
видимого об'єкта), `bDrawScanArea`/`ScanAreaDebugColor`
(контур frustum камери), `bDrawObjectBBox`/`BBoxDebugColor` (footprint-квад по
кутах), `bDrawObjectLabel`/`LabelDebugColor`/`LabelFontScale` (`elementId` як
текст над точкою). Усе — лише при `bSensorEnabled`. Окремо від цього
`bDebugGroundTrace` (Category `Ground`) малює кожну ground-трасу (зелена — влучила,
червона — промах) і логує прогрес розв'язання; його не плутати з жовтими променями
`bDrawRayDebug`.

### `FCustomSurroundingObject`

`Structure/CustomSurroundingObject.h`: `ObjectID` (= `elementId`, ключ сховища),
`ObjectType`, `Latitude`/`Longitude` (середнє кутів, °), `AltitudeMeters` (з JSON,
інформативно), `WorldLocationMeters` (центр footprint, м), `BBoxCornersWorldMeters`
(`TArray<FVector>`, м, після ground-snap), `DistanceMeters`, `bGroundHeightResolved`.
