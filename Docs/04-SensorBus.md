# 04 — Сенсорна шина (ZeroMQ)

Паралельна, opt-in система, що публікує телеметрію БпЛА через ZeroMQ для зовнішніх
споживачів (наземне керування / ML-інструменти), незалежна від аеродинаміки й VFX.

## Контракт: `IUAVSensorInterface`

`Interfaces/UAVSensorInterface.h`. Реалізує кожен сенсорний компонент.

| Член | Опис |
|------|------|
| `bool bSensorEnabled` | Гейт. Default `false` — інертний, доки `AAirplane::RefreshSensorSettings()` не ввімкне |
| `FString GetSensorTopic()` | Ім'я топіка: `"camera"`, `"lidar"`, ... |
| `bool GetLatestFrame(FSensorFrame&)` | Заповнити останнім готовим кадром; `false`, якщо даних ще нема. Викликається в ігровому потоці `USensorBusComponent` кожен тік шини |

## Wire-структура: `FSensorFrame`

`Structure/SensorFrame.h`:

| Поле | Тип | Опис |
|------|-----|------|
| `Topic` | `FString` | Ідентифікатор сенсора, стає частиною ZMQ-повідомлення |
| `Payload` | `TArray<uint8>` | Готовий серіалізований байтовий вміст (JSON-текст або JPEG-байти) |
| `Timestamp` | `double` | Світовий час (с) створення кадру |

## `USensorBusComponent`

`Components/SensorBusComponent.h/.cpp`. Додається у Blueprint літака.

**Властивості (EditAnywhere):**
- `Endpoint` (`"tcp://*:5555"`) — ZMQ **PUB** endpoint. Python приєднується
  `zmq.SUB`.
- `BusRate` (30.0 Гц, clamp 0.1..120) — скільки зведених пакетів на секунду.
  Має ≥ найшвидшого сенсора (зазвичай `MaxEncodeFPS` камери).
- `Sensors : TArray<TObjectPtr<UActorComponent>>` — явний список
  (`IUAVSensorInterface`). Порожньо ⇒ авто-дискавер усіх на власнику в `BeginPlay`.

**`BeginPlay`:**
- `IsEnabledSensors()` — гейт за режимом: `false` для `RecordTarget`; `false` для
  `PlaybackAndTrack`, якщо власник **не** `Player`; інакше `true`. Якщо `false` —
  тік вимкнено.
- Створює `FZmqSocketState` (`ZMQ_PUB`, `ZMQ_SNDHWM = 2` — при переповненні кадри
  дропаються, ігровий потік не блокується), `bind(Endpoint)`.
- Розв'язує сенсори (явний список або авто-дискавер).

**`TickComponent`** — акумулятор часу; кожні `1/BusRate` с → `CollectAndSend()`.

**`CollectAndSend()`:**
1. Опитує кожен розв'язаний сенсор: якщо `bSensorEnabled` і `GetLatestFrame(Frame)`
   → додає в `Frames`. Якщо `Frames` порожній — нічого не шле.
2. Будує **JSON-конверт**:
   `{"timestamp": <bus_time>, "sensors": [{"topic": "...", "timestamp": <sensor_time>}, ...]}`.
3. Шле **одне атомарне ZMQ multipart-повідомлення**:
   - Частина 0 — JSON-конверт;
   - Частини 1..N — сирий `Payload` кожного сенсора, у **тому ж порядку**, що
     масив `sensors` конверта.
   Прапорці: `ZMQ_SNDMORE | ZMQ_DONTWAIT` для всіх, крім останньої частини.
   На винятку (HWM) — дроп, без блокування.

**Python-клієнт:**
```python
parts    = socket.recv_multipart()
envelope = json.loads(parts[0])
for i, s in enumerate(envelope["sensors"], start=1):
    process(s["topic"], parts[i])
```

`EndPlay` — `delete ZmqState`.

## Гейт увімкнення (`AAirplane::RefreshSensorSettings`)

Для кожного сенсора: `C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensor<Тип>`,
де `bSensorsActive` = роль літака відповідає `Subsystem->SensorsMode`
(`Drone` → `Player`/`AutoTracker`, `Target` → `Target`, `None` → ніколи).

## Перелік сенсорів

| Клас | Топік | Тип базового класу | Payload |
|------|-------|--------------------|---------|
| `UAltimeterComponent` | `altimeter` | `UActorComponent` | `{"altitude_m": <float>}` — `Owner->GetActorLocation().Z / 100` |
| `UAttitudeIndicatorComponent` | `attitude_indicator` | `UActorComponent` | `{"roll_deg","pitch_deg","yaw_deg","roll_rate_dps","pitch_rate_dps","yaw_rate_dps"}` — кути з `GetActorRotation()`, швидкості з `Mesh->GetPhysicsAngularVelocityInDegrees()` (0 без фізики) |
| `UCameraInclinationComponent` | `camera_inclination` | `UActorComponent` | `{"pitch_deg": <float>}` — світовий pitch `USceneCaptureComponent2D` (враховує і гімбал, і атитюд літака); + = вгору |
| `UCameraAltitudeComponent` | `camera_altitude` | `UActorComponent` | `{"altitude_m": <float>}` — світовий Z камери / 100 |
| `ULidarComponent` | `lidar` | `USceneComponent` | `{"<ActorName>": <distance_cm>, ...}` — найближчий хіт на актор. Параметри: `Range` (5000 см), `HorizontalRays` (360), `VerticalLayers` (16), `VerticalFOVDeg` (30), `ScanRate` (10 Гц), `CollisionChannel`. Скан через `USensorUtilityLibrary::FindActors` зі свого трансформу |
| `UBBoxDetectionComponent` | `bbox` | `UActorComponent` | `{"<ActorName>": {"Min":{"X","Y"},"Max":{"X","Y"}}, ...}` — OBB актора спроєктований у 2D екрана `USceneCaptureComponent2D`. Актори сцени збираються raycast-sweep-ом один раз; проєкції — щотіку. Параметри як у Lidar (+ `ScanRate` зарезервовано) |
| `UKeyPointDetectionComponent` | `keypoints` | `UActorComponent` | `{"<ActorName>": [{"id","x","y","visible"}, ...], ...}` — `UKeyPointComponent` на виявлених raycast-sweep-ом акторах, спроєктовані на render target спостерігача. `visible=false` за камерою або поза межами кадру; `x/y` віддаються завжди |
| `UDronePositionComponent` | `drone_position` | `UActorComponent` | `{"x_m","y_m","z_m"}` — `Owner->GetActorLocation() / 100` |
| `UGeoPositionDroneComponent` | `drone_geo_position` | `UActorComponent` | `{"latitude","longitude","altitude_m"}` (double). Розв'язує `ACesiumGeoreference::GetDefaultGeoreference`; щотіку `Georeference->GetActorTransform().InverseTransformPosition(loc)` → `TransformUnrealPositionToLongitudeLatitudeHeight` (точний зворот до конверсії в `UCustomSurroundingsScannerComponent`). Діагностично логує розмах крил у Unreal і в геометрії (haversine) |
| `UCameraFrameComponent` | `camera` | `UActorComponent` | Адаптер: `UUAVCameraComponent::GetRGBFrame()` (JPEG-байти). `BeginPlay` шукає `UUAVCameraComponent` на власнику |
| `USegmentationMaskCameraComponent` | `segmentation_mask` | `UActorComponent` | Адаптер: `UUAVCameraComponent::GetMaskFrame()` (JPEG-байти). Потрібен `UUAVCameraComponent::MaskPostProcessMaterial` |
| `UCesiumSurroundingsScannerComponent` | `cesium_objects` | `UActorComponent` | Метадані видимих Cesium 3D Tiles фіч — див. [05-SurroundingsScanners.md](05-SurroundingsScanners.md) |
| `UCustomSurroundingsScannerComponent` | `custom_objects` | `UActorComponent` | Об'єкти з JSON-джерела — див. [05-SurroundingsScanners.md](05-SurroundingsScanners.md) |

### Спільні деталі сенсорів

- Прості сенсори мають `TickComponent`, що при `!bSensorEnabled` одразу виходить;
  оновлює `Latest*` + `LatestTimestamp` + `bHasData`.
- `GetLatestFrame` серіалізує JSON у UTF-8 і кладе в `OutFrame.Payload` (`Reset()`
  + `Append`); повертає `false`, доки `bHasData`/`bHasFrame` не `true`.
- `ULidarComponent` — єдиний `USceneComponent` серед сенсорів (можна розмістити
  де завгодно в ієрархії; трансформ — початок скану). Має власний `ScanRate` +
  `Scan()` (`BlueprintCallable`).

## `USensorUtilityLibrary`

`Util/SensorUtilityLibrary.h/.cpp`. `FindActors(WorldContext, OriginTransform,
ActorToIgnore, Range, HorizontalRays, VerticalLayers, VerticalFOVDeg,
CollisionChannel, bTraceComplex=false) → TArray<FHitResult>` — сферичний
raycast-sweep, спільний для Lidar / BBox / KeyPoint detection.

## `UKeyPointComponent`

`SceneComponent/KeyPoint/KeyPointComponent.h/.cpp` — простий `USceneComponent` з
`PointID : FString`. Розміщується вручну в Blueprint на планері (ніс, кінець крила
тощо). **Не** входить в аеродинамічну ієрархію. Споживачі: `UKeyPointDetectionComponent`
(рантайм) і `ADroneKeyPointDatasetActor` (офлайн-експорт).
