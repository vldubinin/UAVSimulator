# 13 — Об'єкти середовища: РЕБ-зони, вітер, дощ та вуличні вогні

Паралельна, опційна система наземних/просторових об'єктів середовища, що
впливають на політ і/або сенсори — незалежна від аеродинамічної ієрархії
`AAirplane`, але фізично зачіпає `UFlightDynamicsComponent` (вітер) і
`UUAVCameraComponent` (перешкоди РЕБ). Налаштовується або вручну в редакторі,
або через зовнішній Python-інструмент з картою (`Tools/ProjectTools/`).

## `AEnvironmentActorManager`

`Actor/EnvironmentActorManager.h/.cpp` — сцена-актор, єдина точка входу для
обох типів об'єктів. Розміщується вручну в рівні (або спавниться лениво —
див. нижче); `UEnvironmentSectionWidget` (кнопка `ConfigurateEnvActorsBtn`,
`UI/Sections/EnvironmentSectionWidget.cpp`) знаходить наявний через
`UGameplayStatics::GetActorOfClass`, або спавнить новий, якщо в рівні його ще
нема (`EWZoneActorClass`/`WindActorClass` лишаться незаданими, доки їх не
призначать вручну в редакторі — без них `Refresh*()` нічого не спавнить, лише
пише попередження в лог).

**Властивості:**
| Властивість | Роль |
|---|---|
| `EWZoneActorClass : TSubclassOf<AEWZoneActor>` | Клас, що спавниться на кожен запис `EWConfigurations` |
| `EWConfigurations : TArray<FEWZoneConfiguration>` | Джерело правди для спавнених зон РЕБ |
| `WindActorClass : TSubclassOf<AWindActor>` | Клас, що спавниться на кожен запис `WindConfigurations` |
| `WindConfigurations : TArray<FWindVectorConfiguration>` | Джерело правди для спавнених вітрових векторів |

**Методи:**
- `OpenConfigurationTool()` — записує поточні `EWConfigurations`/`WindConfigurations`
  у `Tools/ProjectTools/env_actors.json` (`SaveConfigurationsToFile()`), запускає
  `configurate_env_actors.py` **синхронно** (стартові координати карти — з
  `ACesiumGeoreference` рівня; блокує, доки вікно карти не закриють), тоді
  перечитує той самий файл (`LoadConfigurationsFromFile()`).
- `LoadConfigurationsFromFile()` / `SaveConfigurationsToFile()` — парсять/пишуть
  `env_actors.json`: масиви `"electronic_warfare"` (`latitude`, `longitude`,
  `height`, `radius`) і `"wind"` (`start_latitude/longitude/height`,
  `end_latitude/longitude/height`, `speed`, `radius`). Викликає обидва
  `RefreshEWZones()`/`RefreshWindVectors()` після завантаження.
- `RefreshEWZones()` / `RefreshWindVectors()` — знищують усі раніше спавнені цим
  менеджером актори свого типу й спавнять нові по одному на запис конфігурації,
  застосовуючи позицію/радіус (і швидкість для вітру) через сетери актора.
- `BeginPlay()` — спершу пробує `LoadConfigurationsFromFile()`; якщо файла ще
  нема (перший запуск), спавнить з того, що вже задано вручну в редакторі
  (`EWConfigurations`/`WindConfigurations`).
- `PostEditChangeProperty` (редактор) — правка будь-якого з чотирьох полів вище
  одразу перебудовує відповідну сцену; правка самих `*Configurations` також
  зберігає їх у `env_actors.json`.

**Важливо:** `AEnvironmentActorManager` **ніколи** не торкається
`UUAVSimulationSubsystem` — це виключно робота `AUAVSimulatorGameModeBase`
(див. нижче). Менеджер відповідає лише за життєвий цикл акторів і персистентність
у JSON.

## Зона РЕБ — `AEWZoneActor`

`Actor/EWZoneActor.h/.cpp`. Наочний маркер (напівпрозора сфера
`/Engine/BasicShapes/Sphere.Sphere`, без колізії) + єдине джерело формули
перешкод.

- `Radius : float` (см) — масштабує `SphereVisual`; `SetRadius()`.
- `ZoneMaterial : UMaterialInterface*` — прозорий матеріал сфери.
- Позиція зберігається як `StoredLongitude/Latitude/Height` (градуси/метри) —
  **не** виводиться назад із поточної Unreal-позиції. `SetGeoPosition(Lon, Lat,
  Height)` рахує світову позицію наперед через
  `ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal` і
  ставить актора одним `SetActorLocation`. `SetLongitude`/`SetLatitude` —
  часткові сетери для редакторного UI.
- Підписується на `ACesiumGeoreference::OnGeoreferenceUpdated` і перераховує
  позицію зі збереженого LLH щоразу, коли origin рівня змінюється — захист від
  непередбачуваного порядку `BeginPlay` між акторами (якщо зону заспавнили
  раніше, ніж хтось інший застосував збережений origin).
- `GetInterferenceIntensity(WorldLocation) → float [0,1]` — `1.0` у центрі,
  лінійно спадає до `0.0` на межі `Radius`, `0.0` за нею. **Єдине місце**, де
  рахується ця формула — споживачі (нижче) лише викликають її й беруть
  максимум.
- Приховується з кожного бортового `SceneCaptureComponent2D` через
  `UUAVCameraComponent::HideComponent(SphereVisual)` — лишається видимою лише
  в основній камері.

### Споживання перешкод

`Subsystem/UAVSimulationSubsystem.h` тримає `EWZones :
TArray<TWeakObjectPtr<AEWZoneActor>>` (слабкі вказівники — жодних кешованих
копій позиції/радіуса, щоб нічого не застарівало) + делегат
`OnEWSettingsChanged` + `SetEWSettings(Zones)`.

`AUAVSimulatorGameModeBase::UpdateEWSettings()` наповнює цей список свіжим
`UGameplayStatics::GetAllActorsOfClass(AEWZoneActor::StaticClass())` (бачить і
спавнені менеджером, і вручну розміщені в рівні зони) — викликається і в
`BeginPlay()` (щоб підсистема була готова до `StartSimulation()` з UI), і в
`StartSimulation()`.

`UUAVCameraComponent::UpdateEWInterference()` кешує список локально, оновлює
його на `OnEWSettingsChanged`, і щокадру лінійно сканує, беручи **максимум**
`GetInterferenceIntensity(OwnerLocation)` серед усіх зон (не сума — навмисно,
щоб інтенсивність ніколи не перевищувала 1.0). Результат — скалярні параметри
пост-процес матеріалу (`Interference_Intensity`, `Distortion_Strength`,
`Noise_Intensity`).

## Вітровий вектор — `AWindActor`

`Actor/WindActor.h/.cpp`. Наочний маркер (паралелепіпед `BoxVisual` +
`ArrowVisual`) + **реальний фізичний вплив** на аеродинаміку (на відміну від
раніших сесій — тепер не суто візуальний).

- `Radius : float` (см) — переріз (Y/Z) паралелепіпеда рахується як квадрат,
  описаний навколо циліндра цього радіуса (сторона = `2×Radius`); довжина (X) —
  відстань між геоточками Start і End. `SetRadius()`.
- `Speed : float` (см/с, Unreal-native — **не** ті самі одиниці, що
  `FWindVectorConfiguration::Speed`, який лишається м/с; конвертація `×100`
  відбувається в `EnvironmentActorManager::RefreshWindVectors()`, за тим самим
  принципом, що й для `Radius`). `SetSpeed()`.
- `SetGeoPositions(StartLon, StartLat, StartH, EndLon, EndLat, EndH)` —
  атомарно задає обидві геоточки, рахує світові позиції наперед (той самий
  принцип LLH-джерела-правди, що й `AEWZoneActor`), ставить актора в **середину**
  відрізка (куб рівномірно розтягується в обидва боки), обертає вздовж
  напрямку Start→End (`GetActorForwardVector()` = напрямок вітру) і
  масштабує `BoxVisual`.
- Так само підписується на `ACesiumGeoreference::OnGeoreferenceUpdated`.
- `GetWindVelocityAtLocation(WorldLocation) → FVector` (см/с) — **єдине
  джерело правди** щодо впливу цього вектора в заданій світовій точці:
  1. Оскільки `BoxVisual` — root-компонент, `GetActorTransform()` вже включає
     його нерівномірний масштаб. `GetActorTransform().InverseTransformPosition(
     WorldLocation)` тому одразу дає точку в **немасштабованому** просторі куба
     (півсторона = 50 см на кожній осі) — окремо кешувати довжину/радіус не
     треба.
  2. Належність перевіряється відстанню Чебишева в нормалізованих одиницях;
     поза кубом — `FVector::ZeroVector`.
  3. Плавне загасання (smoothstep) в останніх 15% кожної півосі — щоб уникнути
     стрибка сили, коли сегмент поверхні перетинає межу зони.
  4. Напрямок — `GetActorForwardVector()`, величина — `Speed × Factor`.
- Приховується з бортової камери так само, як `AEWZoneActor` (`BoxVisual` +
  `ArrowVisual` через `HideComponent`).

### Споживання вітру — вплив на фізику польоту

`UUAVSimulationSubsystem::WindVectors : TArray<TWeakObjectPtr<AWindActor>>` —
точне дзеркало `EWZones` (слабкі вказівники, `OnWindSettingsChanged`,
`SetWindSettings`). Наповнюється `AUAVSimulatorGameModeBase::UpdateWindSettings()`
(структурна копія `UpdateEWSettings()`, ті самі дві точки виклику).

На відміну від РЕБ, `UUAVSimulationSubsystem::GetWindVelocityAtLocation(
WorldLocation) → FVector` сама сумує внесок усіх векторів (**векторна сума**,
не максимум — вітер фізична швидкість, тож поля мають складатися; той самий
принцип, що й у вже наявній системі вихрового сліду,
`UFlightDynamicsComponent::GetInducedVelocity`, яка теж сумує кілька внесків
швидкості в точці).

Єдина точка дотику з літаком — `USubAerodynamicSurfaceSC::CalculateForcesOnSubSurface()`
(`SceneComponent/SubAerodynamicSurface/SubAerodynamicSurfaceSC.cpp`): перед
побудовою `WorldAirVelocity` опитує підсистему в **своїй** `CenterOfPressureInWorld`
(своя точка для кожного сегмента поверхні окремо):

```cpp
FVector Wind = FVector::ZeroVector;
if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
    Wind = Subsystem->GetWindVelocityAtLocation(CenterOfPressureInWorld);

FVector WorldAirVelocity = -LinearVelocity + Wind - RotationalVelocity;
```

Це дає грануляцію «по частинах» безкоштовно: крило, що залетіло в зону вітру,
й фюзеляж поза нею отримують коректно різний внесок у AoA/підйом/опір/момент —
без жодних змін в `AerodynamicSurfaceSC`, `FlightDynamicsComponent`,
`UAVPhysicsStateComponent` чи `AAirplane` (єдине місце в аеродинамічному
конвеєрі, що взагалі знає про існування вітру). Під `bVisualizeForces`
малюється блакитна стрілка вітру поруч із зеленою стрілкою результуючої сили
(`AerodynamicDebugRenderer::DrawForceArrow`).

## Дощ — `ARainEffectManager`

`Actor/RainEffectManager.h/.cpp`. На відміну від `AEnvironmentActorManager`, не
конфігурується через `env_actors.json` (дощ глобальний, без гео-прив'язки) і не
знає про `AEnvironmentActorManager` взагалі — окремий, самодостатній актор,
розміщений вручну в рівні (по одному на рівень).

- `RainSystem : UNiagaraSystem*` — Niagara-система дощу (`Content/FX/NS_Rain`),
  що спавниться над кожним `AAirplane`. Поки не задана — `RescanAirplanes()`
  виходить одразу, нічого не спавнячи (той самий принцип, що й
  `RefreshEWZones()`/`RefreshWindVectors()` без відповідного `*ActorClass`).
- `RainOffset : FVector` (см, дефолт `(0,0,2000)`) — зміщення дощу відносно
  `GetActorLocation()` літака по всіх осях (світові, без урахування орієнтації
  літака — узгоджено з тим, що дощ ніколи не обертається): Z — висота над
  літаком, X/Y — горизонтальний зсув (наприклад, з випередженням напрямку
  польоту чи вбік).
- `RescanInterval` (сек, дефолт 1.0) — як часто пере-сканувати світ через
  `UGameplayStatics::GetAllActorsOfClass(AAirplane::StaticClass())`, щоб
  підхопити нові літаки (спавн через `StartSimulation()`) і прибрати ефекти
  знищених (`StopSimulation()`, зміна режиму) — позиція вже відстежуваних
  ефектів оновлюється **щотіку**, незалежно від цього інтервалу.

**Принцип нульової зв'язаності**: `AAirplane.h/.cpp` і його Blueprint нічого не
знають про Niagara чи про `ARainEffectManager` — весь зв'язок односторонній,
ззовні. Менеджер тримає `TMap<TWeakObjectPtr<AAirplane>, UNiagaraComponent*>
ActiveRainEffects`: для кожного знайденого `AAirplane`, якого ще нема в мапі,
спавнить окремий `UNiagaraComponent` через
`UNiagaraFunctionLibrary::SpawnSystemAtLocation` (**без** `AttachTo`,
`bAutoDestroy = false` — часом життя керує сам менеджер); щотіку виставляє
йому `SetWorldLocation(Airplane->GetActorLocation() + RainOffset)` з нульовою
ротацією, тож дощ завжди на заданому зміщенні й завжди падає прямо вниз,
незалежно від крену/тангажу літака. `RescanAirplanes()` також
знищує `UNiagaraComponent` і прибирає запис для будь-якого `TWeakObjectPtr`,
що став stale (літак знищено).

**`RainIntensity`** (`SetRainIntensity()`/`GetRainIntensity()`, дефолт `1.0`,
знизу обмежено нулем; `ClampMax 5` у `UPROPERTY` діє лише в редакторі) — **єдине**
поле, що керує дощем: одночасно і значення Spawn Rate, і вимикач. `0` — негайно знищує всі активні `UNiagaraComponent`
(`DestroyAllRainEffects()`, спільна з `EndPlay()`) і зупиняє `Tick()`/спавн
нових ефектів; будь-яке значення `> 0` — одразу перескановує світ (якщо перед
цим було `0`) і прокидається як User Parameter (Float) на кожен активний
`UNiagaraComponent` через `SetFloatParameter(TEXT("Intensity"),
RainIntensity)` (`ApplyIntensity()`, викликається і при зміні, і одразу при
спавні нового ефекту в `RescanAirplanes()`). **`NS_Rain` експонує User Parameter з точно такою назвою (`Intensity`, тип Float), і його значення напряму є `SpawnRate` емітера** (Linked Variable, а не множник) — тобто `RainIntensity` = частинок за секунду; діапазон `SpinBoxRainIntensity` — 0–150000 (деталі та застереження — `12-Niagara.md`, розділ `NS_Rain`). Вкл/викл при переході через `0` працює завжди, незалежно від User Parameter. Керується єдиним `SpinBoxRainIntensity`
у `UEnvironmentSectionWidget` (нема окремого чекбокса вкл/викл), персиститься
в `UEnvironmentSettingsSave::RainIntensity`. `UEnvironmentSectionWidget::
GetRainEffectManager()` лениво спавнить `ARainEffectManager`, якщо в рівні
його ще нема — так само, як `GetEnvironmentActorManager()`.

## Нічні вуличні вогні — `AStreetLightsManager`

`Actor/StreetLightsManager.h/.cpp`. На відміну від `AEnvironmentActorManager`
(конфігурація через `env_actors.json`) і `ARainEffectManager` (глобальний,
без гео-прив'язки), цей менеджер не реалізує власне виявлення об'єктів — він
читає готові результати сканерів, що сидять на `AAirplane`. **Джерело обирається**
(`DataSource`, див. нижче): `UCustomSurroundingsScannerComponent` (за замовчуванням;
лениво додається на літак, якщо його там ще нема) або
`UCesiumSurroundingsScannerComponent` (лише вже наявний на літаку). Розміщується
вручну в рівні (або лениво спавниться
`UEnvironmentSectionWidget::GetStreetLightsManager()`, так само як
`GetRainEffectManager()`) — один менеджер на рівень.

**Керування (`UEnvironmentSectionWidget`):**
`SpinBoxStreetLightsBrightness` (0–100, `0` = вимкнено) і
`ComboBoxStreetLightsDataSource` (`Custom` / `Cesium`); обидва зберігаються в
`UEnvironmentSettingsSave` і застосовуються при старті меню.

**Властивості `AStreetLightsManager`:**
| Властивість | Дефолт | Роль |
|---|---|---|
| `StreetLightsSystem : UNiagaraSystem*` | — | `NS_StreetLights`; поки не задано — нічого не спавниться |
| `Brightness` (приватна, `SetBrightness/GetBrightness`) | 0 | 0–100; `0` деактивує Niagara |
| `DataSource` (`SetDataSource/GetDataSource`) | `Custom` | Джерело будівель; зміна скидає всі відстежувані вогні |
| `ScannerScanRadiusMeters` / `CollisionChannel` | 2000 / Visibility | Застосовуються лише до щойно створеного `UCustomSurroundingsScannerComponent` |
| `MaxTrackingDistanceMeters` | 0 | `0` = вогні ніколи не прибираються; `>0` — відсікання за відстанню від літака |
| `MinRebuildIntervalSeconds` | 2 | Тротлінг перебудови масиву Niagara (кожна = `Activate(true)`) |
| `LightSpacingMeters` / `LightHeightMeters` | 18 / 4 | Крок вогнів по периметру / висота "стовпа" |
| `CesiumFootprintMinSizeMeters` / `…MaxSizeMeters` | 15 / 40 | Розмір випадкового прямокутника для джерела `Cesium` |
| `bDrawDebugFootprints`, `FootprintDebugColor`, `LightDebugColor` | false | Debug-контури й позиції вогнів |
| `TrackedBuildings` | — | `VisibleAnywhere`-дзеркало `TrackedBuildingsMap` для інспекції |

### Виявлення — споживання сканерів (`Custom` за замовчуванням)

**Історія:** попередні версії цього класу самі обчислювали або сканували
позицію будівлі — (1) з lat/long Cesium-метаданих, обчислюючи світову
позицію наперед і трейсячи туди для перевірки (систематично проминало все:
кривина Землі на віддалених від Georeference-початку точках, LOD-неточність
метаданих); (2) власна сітка вертикальних трейсів навколо XY літака
(проблема була в тому, що Cesium підвантажує тайли попереду з затримкою —
сітка, центрована на поточній позиції, майже завжди застає нове лише
позаду); (3) `UCesiumSurroundingsScannerComponent` (замітає конус огляду
камери, тож бачить те, що попереду, але дає лише сиру точку влучання без
готового footprint і без гарантії, що тайли попереду вже мають колізію). Пізніше
цей сканер повернуто як опційне джерело `Cesium` (див. «Джерело даних») — з
випадковим прямокутником навколо точки влучання.

**Поточний підхід** (для джерела `Custom`) — `GetOrCreateScannerFor()` знаходить (або лениво додає
й реєструє) `UCustomSurroundingsScannerComponent` на кожному `AAirplane`, із
застосованими `ScannerScanRadiusMeters`/`CollisionChannel`. Цей компонент
сам прив'язує кожен із чотирьох кутів свого `bbox` до поверхні тайла Cesium
(`ResolveGroundHeights`) і віддає в `LatestScanResults` уже ГОТОВИЙ
footprint (`FCustomSurroundingObject::BBoxCornersWorldMeters`) — жодного
трейсу тут більше не потрібно.

**Важливо:** джерело об'єктів `UCustomSurroundingsScannerComponent` —
**не** автоматичне "усі будівлі поруч", а фіксований `ObjectsJson` (дефолт —
заглушка з двох прикладних будівель, див. `Docs/05-SurroundingsScanners.md`).
Щоб вогні з'являлися біля реальних будівель на маршруті літака,
`ObjectsJson` щойно заспавненого сканера потрібно заповнити вручну (напр.
через деталі компонента на Blueprint літака, або призначивши власний
підклас/дефолт) реальними id/bbox — `AStreetLightsManager` сам туди нічого
не генерує. Якщо сканер уже є на Blueprint літака (Cessna_172 має обидва),
менеджер використовує саме його — вогні йдуть із тих самих даних, що видно на
екрані через debug-промені цього сканера.

Debug-промені сканерів вимикаються прапорцем `bDrawRayDebug`
(`UCustomSurroundingsScannerComponent` і `UCesiumSurroundingsScannerComponent`,
див. `05-SurroundingsScanners.md`).

### Footprint і розміщення вогнів

`Scan()` (`Tick()`, щокадру — сама робота дешева, читає вже обчислений
масив): для кожного `AAirplane` — `GetOrCreateScannerFor()`, потім кожен
об'єкт `LatestScanResults` із `bGroundHeightResolved` зливається за своїм
`ObjectID` у `TrackedBuildingsMap` (якщо ще не відстежується).
`BuildLightsForObject()` розставляє вогні вздовж периметра готового
footprint (`BBoxCornersWorldMeters`) із кроком `LightSpacingMeters` (дефолт
18 м); висота кожного — Z відповідної точки периметра (уже на рівні
рельєфу, інтерпольований між кутами) + `LightHeightMeters` (дефолт 4 м).
Жодного додаткового ground-trace тут не потрібно — сканер уже все прив'язав.

Порядок кутів `bbox` у сканері (`x_min → x_max → y_min → y_max`) не гарантує обхід
по периметру (у деяких об'єктів `y_min`/`y_max` міняються місцями), тому
`BuildLightsForObject()` спершу переупорядковує чотири кути: із трьох циклічних
обходів обирає той, що має найменший периметр (без самоперетину) — інакше
лінії вогнів утворюють "метелика".

### Джерело даних (`DataSource`)

`AStreetLightsManager::SetDataSource(EStreetLightsDataSource)` (`Entity/StreetLightsDataSource.h`)
обирає, з якого сканера береться список будівель. Керується
`ComboBoxStreetLightsDataSource` (`UComboBoxString`, `OptionalWidget`; опції
"Custom"/"Cesium" додаються з C++) у `UEnvironmentSectionWidget`, зберігається в
`UEnvironmentSettingsSave::StreetLightsDataSource`.

| Джерело | Сканер | Що виходить |
|---------|--------|-------------|
| `Custom` (дефолт) | `UCustomSurroundingsScannerComponent` | footprint із чотирьох кутів → вогні по периметру |
| `Cesium` | `UCesiumSurroundingsScannerComponent` | метадані не дають контуру → навколо `HitLocationMeters` будується **прямокутник випадкового розміру** (`CesiumFootprintMin/MaxSizeMeters`, дефолт 15–40 м, випадковий поворот; генератор засіяний хешем `ObjectID`, тож прямокутник стабільний) і вогні йдуть по його периметру |

Для `Cesium` менеджер сам сканер не створює (він потребує бортової камери) — береться
той, що вже є на літаку (Blueprint), і працює лише коли камера активна.
Перемикання джерела скидає всі вже відстежувані вогні (різні `ObjectID` і геометрія).

### Видалення — лише опційно

`RunValiditySweep()` прибирає будівлі, чий центр footprint далі за
`MaxTrackingDistanceMeters` від УСІХ `AAirplane`, **лише якщо це значення > 0**.
Дефолт — `0` (вогні ніколи не прибираються): кожне видалення/додавання
перезапускає Niagara-систему (`Activate(true)`) і вогні на мить зникають, тож
відсікання за відстанню давало add/remove-цикл із "миготінням" навіть у полі зору.
Перебудова масиву Niagara тротлиться `MinRebuildIntervalSeconds` (дефолт 2 с) —
нові будівлі накопичуються й додаються одним пакетом.

### Рендер і керування

Один постійний `UNiagaraComponent` (`StreetLightsSystem`, спавниться
`UNiagaraFunctionLibrary::SpawnSystemAtLocation` з `bAutoDestroy=false`, як
`RainSystem` у `ARainEffectManager`) — `RebuildNiagaraArrays()` штовхає
плаский `TArray<FVector>` позицій усіх вогнів через
`UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector`
(User Parameter `"LightPositions"`, той самий підхід, що `WakePositions` у
`UAeroVisualizerComponent`) щоразу, коли `TrackedBuildingsMap` змінюється —
масштабується на тисячі вогнів без per-light акторів/компонентів.

**`Brightness`** (`SetBrightness()`/`GetBrightness()`, [0,100], дефолт 0) —
єдине поле керування, той самий принцип "інтенсивність і вимикач в одному",
що `RainIntensity`: `0` деактивує `UNiagaraComponent`
(`NiagaraComp->Deactivate()`), `>0` активує і прокидає User Parameter
`"Brightness"` (Float, [0,1] — `Brightness/100`). `StreetLightsSystem` має
експонувати цей параметр і використовувати його (типово — множником на
розмір/яскравість спрайта), інакше зміна значення ні на що не вплине.
Керується єдиним `SpinBoxStreetLightsBrightness` у `UEnvironmentSectionWidget`
(нема окремого чекбокса вкл/викл), персиститься в
`UEnvironmentSettingsSave::StreetLightsBrightness`.
`UEnvironmentSectionWidget::GetStreetLightsManager()` лениво спавнить
`AStreetLightsManager`, якщо в рівні його ще нема — так само, як
`GetRainEffectManager()`.

### `NS_StreetLights` (`Content/FX/NS_StreetLights`)

Клонований з `NS_VLMFlow` (щоб успадкувати вже робочий GPU sprite-рендерер),
очищений від хвильових модулів вихорового сліду й переналаштований під один
User Parameter `Brightness` (Float, [0,1]) і один масив
`LightPositions` (`NiagaraDataInterfaceArrayFloat3`). Три знахідки з
практичної розбудови через `unreal-mcp`, важливі для будь-якого майбутнього
редагування цього ассету:

1. **Custom Hlsl-вирази не можуть викликати функції Data Interface масивів
   на User-параметрі** — `User.LightPositions.Get(...)`/`.Length()` валять
   компіляцію (`GetDuplicatedDataInterfaceCDOForClass failed` /
   `Cannot Set external constant`), навіть якщо параметр коректно
   "зустрінутий" раніше в графі. Робочий шлях — лише штатні dynamic input
   ассети: **`Position`** входу `InitializeParticle` встановлено на
   `SelectVectorFromArray` (`/Niagara/DynamicInputs/Arrays/`), чий власний
   вхід `Vector Selection Array` прив'язаний (Linked Variable) до
   `User.LightPositions`.
2. **`SelectVectorFromArray` вибирає ВИПАДКОВИЙ елемент масиву на кожен
   спавн** — точного відповідника "елемент масиву за `ExecIndex()`" немає.
   Тому емітер працює через **`SpawnBurst_Instantaneous`** (Spawn Count =
   `User.TargetSpawnCount`, Linked Variable) із величезним `Lifetime`
   (`InitializeParticle::Lifetime = 86400`), а C++ після кожної зміни масиву
   робить `Activate(true)` (повний reset → новий burst одразу на весь масив).
3. **User-параметри — read-only в графі**, тому кількість вогнів
   `AStreetLightsManager::RebuildNiagaraArrays()` рахує в C++ і штовхає як
   plain User Parameter **`TargetSpawnCount`** (Int32).

**Bounds / culling.** GPU-емітер не рахує bounds частинок сам; без фіксованих
bounds система відсікається за крихітною коробкою навколо актора і вогні
зникають навіть у полі зору. Тому `RebuildNiagaraArrays()` викликає
`SetSystemFixedBounds()` з bounding-box усіх вогнів (+100 м запасу) і
`SetAllowScalability(false)`. Frustum culling лишається, але за реальною
областю вогнів.

`Color` (`InitializeParticle`, Direct Set) — HLSL-вираз
`float4(1.0, 0.75, 0.4, 1.0) * User.Brightness` (плоске звернення до
User-параметра в Custom Hlsl працює нормально — ламаються лише виклики
функцій Data Interface, не самі значення). `Uniform Sprite Size = 40`
(фіксований, не залежить від Brightness — яскравість "вимикання" вже дає
`Brightness=0` через колір/деактивацію компонента). Рендерер — стандартний
`DefaultSpriteMaterial`; для продакшн-вигляду варто замінити на власний
емісивний матеріал вуличного ліхтаря.

## Нічне небо: зорі — `M_Stars` / `MI_Stars`

`Content/Environment/M_Stars` (батьківський матеріал) і `MI_Stars` (інстанс) — матеріал
меша `StarsSphere` (`SM_SkySphere`), який додано вручну в `CesiumSunSky_0`. Параметри
керуються з `UEnvironmentSectionWidget::UpdateNightVisuals()` (див. `08-UI-and-Settings.md`),
а сам ефект — це **процедурне 3D-поле зір без текстур і UV-швів**.

**Параметри матеріалу:**
| Параметр | Дефолт | Роль |
|---|---|---|
| `NightFactor` | 0 | Загальна яскравість зір [0,1]; C++ рахує `clamp(-Elevation/10, 0, 1)` від нахилу сонця (повний нуль на 10° над горизонтом, максимум на 10° під ним) |
| `StarsIntensity` | 8 | Множник яскравості |
| `StarsDensity` | 250 | Масштаб клітинок: чим більше, тим дрібніше й більше зір |
| `StarsThreshold` | 0.985 | Поріг хеша: зірка є в клітинці, якщо `hash > StarsThreshold`; **менший поріг = більше зір** |
| `StarsPointSize` | 0.12 | Базовий радіус зірки (у частках клітинки) |
| `StarsSharpness` | 6 | Степінь у вузлі `Power` над текстурою `T_Sky_Stars` (текстурна гілка графа; чи підключена вона до виходу матеріалу — не перевірялося, зорі, що описані нижче, від неї не залежать) |

**Алгоритм (Custom HLSL-вузол):**
1. `p = normalize(CameraVector) * StarsDensity`; `cell = floor(p)`, `f = frac(p) - 0.5`.
2. Цілочисельний PCG-подібний хеш від `cell` дає `rnd1` (чи є зірка), `rnd2` (яскравість
   `0.3–1.0`), `rnd3` (радіус `0.3–1.0 × StarsPointSize`); другий хеш з іншим зерном —
   зсув зірки всередині клітинки.
3. Зірка — диск радіуса `trueRadius` на відстані `dist = |f − jitter|`; край згладжується
   `smoothstep` по `aa = fwidth(dist)`. Зорі, менші за піксель, розширюються до пікселя, а їхня
   яскравість зменшується пропорційно площі (`energyScale`), щоб не мерехтіли.
4. Результат множиться на `NightFactor` і `StarsIntensity`.

**Виправлення «кіл» при великій щільності й низькому порозі.** Раніше зорі мали три дефекти:
- усі зорі стояли **строго в центрах клітинок** — коли активна більшість клітинок, видно
  регулярну 3D-решітку, а її проєкція на сферу дає концентричні кола (муар). Тепер позицію
  зірки випадково зсунуто всередині клітинки (на `±(0.5 − trueRadius)`, щоб зірка не
  обрізалась межею клітинки);
- хеш `frac(sin(dot(cell, …)) * 43758.5453)` втрачав точність на великих координатах клітинок
  (`StarsDensity` ≈ 250 → координати сотні) і давав впорядковані візерунки — замінено на
  цілочисельний хеш без `sin()`;
- `fwidth(dist)` викликався всередині `if (rnd1 > Threshold)` (неоднорідний потік, похідні
  недостовірні) — тепер відстань і `fwidth` рахуються безумовно, а поріг застосовується
  наприкінці (`(rnd1 > Threshold) ? mask * brightness : 0`).

Імена й тип параметрів матеріалу не змінювалися, тож `UpdateNightVisuals()` і збережені
налаштування (`StarsDensity/Threshold/PointSize/Intensity` в `UEnvironmentSettingsSave`)
працюють без змін; конфігурація зір лише виглядатиме інакше (інший розподіл).

## Структури-конфігурації

- `FEWZoneConfiguration` (`Structure/EWZoneConfiguration.h`) — `Latitude`,
  `Longitude`, `Height` (м), `Radius` (м).
- `FWindVectorConfiguration` (`Structure/WindVectorConfiguration.h`) —
  `StartLatitude/Longitude/Height`, `EndLatitude/Longitude/Height` (град/м),
  `Speed` (м/с, дефолт 5.0), `Radius` (м, дефолт 50.0).

Обидві — точний відповідник того, що `configurate_env_actors.py` пише/читає з
`env_actors.json`; поля іменовані так само (snake_case у JSON ↔
PascalCase-без-префікса в C++).

## Інструмент карти — `Tools/ProjectTools/configurate_env_actors.py`

Standalone Tkinter-вікно з інтерактивною картою (`tkintermapview`,
Google-тайли), що відкриває `AEnvironmentActorManager::OpenConfigurationTool()`
синхронно (блокує гру, доки не закриють вікно). Залежності: `pip install
tkintermapview Pillow`.

**При старті** підхоплює `env_actors.json` (якщо є) і одразу малює всі наявні
зони РЕБ і вітрові вектори.

**Зони РЕБ** — кнопка «Add Electronic warfare» → один клік на карті ставить
червоний маркер «EW» (дефолт: висота 0 м, радіус 50 м). Клік по маркеру
відкриває модалку (Latitude/Longitude/Height/Radius + Save/Close/Delete).

**Вітрові вектори** — кнопка «Add Wind» → **два** кліки на карті: перший —
початок, другий — кінець. Малюється синя стрілка (лінія + повернутий
наконечник, кут — `bearing_deg()`) + прямокутник навколо неї (вісь = стрілка,
`Radius` — відступ вліво/вправо, `wind_rectangle_points()`). Клік по стрілці
або прямокутнику відкриває модалку: групи Start/End (Latitude/Longitude/
Height), поля Speed (м/с) і Radius (м), канвас side-view, що вживу
перемальовується при зміні полів Height (показує нахил вектора), Save/Close/
Delete.

**Кнопка SAVE** перезаписує обидва масиви (`"electronic_warfare"`, `"wind"`) у
`env_actors.json`, звідки їх перечитує `LoadConfigurationsFromFile()` після
закриття вікна.

## Формат `env_actors.json`

```json
{
  "electronic_warfare": [
    { "latitude": 50.045, "longitude": 36.291, "height": 300, "radius": 50 }
  ],
  "wind": [
    {
      "start_latitude": 50.049, "start_longitude": 36.287, "start_height": 500,
      "end_latitude": 50.046, "end_longitude": 36.288, "end_height": 700,
      "speed": 5.0, "radius": 50.0
    }
  ]
}
```

Обидва масиви завжди присутні (навіть порожні) — `AEnvironmentActorManager::
SaveConfigurationsToFile()` і `configurate_env_actors.py::on_save_clicked()`
пишуть їх симетрично, тож жодна сторона не втрачає дані іншої між запусками.
