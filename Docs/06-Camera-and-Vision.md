# 06 — Бортова камера та комп'ютерний зір

## `UUAVCameraComponent`

`Components/UAVCameraComponent.h/.cpp`. **`UActorComponent`** (не `USceneComponent`
— без трансформу, не кріпиться до сцени). **Не CDO** — `AAirplane` створює його
ліниво в `RefreshConfigurations()`, коли камера активна для ролі цього літака
(`Subsystem->OnboardCameraMode`). Керує RGB-захопленням, OpenCV-обробкою, маскою
сегментації та per-tick стабільними JPEG-навантаженнями.

### Ресурси

- Знаходить `USceneCaptureComponent2D` на власнику (розміщується у Blueprint),
  бере його як джерело кадрів.
- `RenderTarget` — `PF_B8G8R8A8`, **640×480** (`CVWidth`/`CVHeight`,
  `static constexpr`). `CaptureComponent->CaptureSource = SCS_FinalColorLDR`.
- `OutputTexture : UTexture2D*` (`VisibleAnywhere`, `PF_B8G8R8A8`, transient) —
  оброблений вихід; біндиться у віджеті/матеріалі. `AAirplane::GetCameraOutputTexture()`
  його віддає.
- `MaskRenderTarget` — окремий RT `PF_B8G8R8A8` 640×480 для маски сегментації.
- `MaskPostProcessMaterial : UMaterialInterface*` (EditAnywhere) — матеріал, що
  перетворює Custom Stencil на Ч/Б-маску. Заданий ⇒ захоплення маски йде поруч із
  RGB щотіку.

### Обробка кадру

**`TickComponent`** (при `bIsProcessingEnabled`): під локами копіює останні
готові payload-и енкодерів у tick-стабільний кеш, тоді `ProcessFrame()` +
`CaptureMask()`.

**`ProcessFrame()`**:
1. `RenderTarget->ReadPixels(ColorBuffer)`.
2. Обгортає буфер у `cv::Mat FrameBGRA(CVHeight, CVWidth, CV_8UC4, ...)` →
   `ProcessedFrameBuffer` (точка розширення під OpenCV-обробку).
3. Якщо минуло `≥ MinEncodeInterval` (`1/MaxEncodeFPS`) — під локом копіює BGRA в
   `PendingRGBBGRA`, ставить timestamp, тригерить `RGBFrameReadyEvent` (енкодер).
4. `UploadToTexture()` → `OutputTexture`.

**`CaptureMask()`** — позичає `CaptureComponent`, наводить на `MaskRenderTarget`,
інжектить `MaskPostProcessMaterial`, `CaptureScene()`, `ReadPixels` (флашить
render-потік — команда гарантовано виконана), відновлює. Маска синхронна з RGB
того самого тіку. Тригерить `MaskFrameReadyEvent`.

### JPEG-кодування (фонові потоки)

Окремий `FRunnableThread` для RGB (`RGBEncoderLoop`) і для маски
(`MaskEncoderLoop`). Кожен чекає на `FEvent`, бере pending-BGRA під локом,
кодує через `IImageWrapperModule` → `EImageFormat::JPEG` (`SetRaw(..., ERGBFormat::BGRA, 8)`),
кладе в `LatestRGBPayload` / `LatestMaskPayload` під локом.

**`GetRGBFrame(OutPayload, OutTimestamp)`** / **`GetMaskFrame(...)`** — віддають
tick-стабільний JPEG-payload (кілька викликів в одному тіку → той самий знімок).
Ігровий потік. Маска — лише коли `MaskPostProcessMaterial` заданий.

Споживачі: `UCameraFrameComponent` (топік `camera`), `USegmentationMaskCameraComponent`
(топік `segmentation_mask`) — тонкі адаптери в сенсорну шину.

### Параметри стрімінгу

- `JpegQuality` (80, 1..100).
- `MaxEncodeFPS` (30, 1..120) — макс. JPEG-кодувань/с для обох потоків
  (`MinEncodeInterval = 1/30` за замовч.).

### FOV / інтринсики

`ComputeFOV(HFovDeg)` — `HorizontalFOVDeg = HFovDeg` (з `CaptureComponent->FOVAngle`,
горизонтальний); `VerticalFOVDeg` виводиться через aspect ratio `CVWidth/CVHeight`.
`HorizontalFOVDeg`/`VerticalFOVDeg` — `VisibleAnywhere`, читаються сканерами
оточення для побудови сітки sweep-у. `LogCameraIntrinsics()` друкує фокус у
пікселях (`FocalPx = (CVWidth·0.5)/tan(HFov/2)`).

### Реєстрація камери в Cesium

`USceneCaptureComponent2D` всередині Blueprint-pawn Cesium сам по собі не бачить
(на відміну від головної камери гравця, редакторних viewport-ів чи
standalone-акторів `ASceneCapture2D`). Тому:

- `ResolveCesiumCameraManager()` — знаходить/спавнить `ACesiumCameraManager` світу
  (лінивий resolve, self-null).
- `SyncCesiumSceneCaptureCamera()` (щокадру, при активній камері) — дзеркалить
  захоплення в `ACesiumCameraManager` як `FCesiumCamera` (`AddCamera` / оновлення),
  щоб Cesium стрімив/рефайнив тайли під цей ракурс. Гейт як у
  `ACesium3DTileset::GetSceneCaptures`: перспектива, розмірений RT, валідний FOV.
- `UnregisterCesiumSceneCaptureCamera()` — `RemoveCamera` при вимкненні/тіардауні.

### `SetCameraProcessingEnabled(bool)`

Вмикає/вимикає весь конвеєр (тік, потоки, Cesium-реєстрацію). Викликається з
`AAirplane::RefreshConfigurations()`.

## Конвеєр маски сегментації

- `r.CustomDepth=3` (`Config/DefaultEngine.ini`) — потрібен для Custom Stencil.
  **Не відкочувати**, поки `USegmentationMaskCameraComponent` у вжитку.
- Актори, які треба бачити на масці, мають писати Custom Stencil; матеріал
  `MaskPostProcessMaterial` конвертує його у Ч/Б.

## Перешкоди РЕБ (пост-процес)

Не заготовка — активний, підключений конвеєр. Матеріал
`Content/Materials/M_EW_Interference.uasset` додається вручну до
`CaptureComponent->PostProcessMaterials`.

- `InitEWInterferenceMIDs()` (`BeginPlay`) — конвертує кожен пост-процес
  матеріал захоплення в `UMaterialInstanceDynamic`, щоб можна було міняти
  скалярні параметри рантаймом.
- `EWZones : TArray<TWeakObjectPtr<AEWZoneActor>>` — кешується з
  `UUAVSimulationSubsystem::EWZones`, оновлюється підпискою на
  `OnEWSettingsChanged` (`EWSettingsChangedHandle`).
- `UpdateEWInterference()` (щокадру) — бере **максимум**
  `AEWZoneActor::GetInterferenceIntensity(OwnerLocation)` серед `EWZones`,
  виставляє `Interference_Intensity`/`Distortion_Strength`/`Noise_Intensity` на
  кожному MID. Активно автоматично для будь-якого літака в радіусі дії хоча б
  однієї зони — окремого вмикача немає. Деталі формули — `13-Environment-Actors.md`.

## Приховане з бортової камери

`BeginPlay` явно ховає з `CaptureComponent` (`HideComponent`) те, що не має
з'являтись у потоці бортової камери, лишаючись видимим лише в основній камері
редактора/гравця:

- усі три line-batcher світу (`World`/`WorldPersistent`/`Foreground`) — інакше
  будь-яка відладочна візуалізація (`DrawDebugLine`/`Box`/`Sphere`,
  напр. промені сканерів оточення) протікала б у кадр камери;
- `AEWZoneActor::SphereVisual` кожної зони РЕБ на сцені;
- `AWindActor::BoxVisual` і `AWindActor::ArrowVisual` кожного вітрового вектора.

## UI камери — `UCameraViewWidget`

`UI/CameraViewWidget.h/.cpp` — тримає `AAirplane*` через `SetAirplane()`. Фактичний
бінд текстури до `GetCameraOutputTexture()` — у UMG-Blueprint, не в C++.
