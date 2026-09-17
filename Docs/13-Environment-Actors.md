# 13 — Об'єкти середовища: РЕБ-зони, вітер та дощ

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
клемп `[0, 5]`) — **єдине** поле, що керує дощем: одночасно і множник
інтенсивності, і вимикач. `0` — негайно знищує всі активні `UNiagaraComponent`
(`DestroyAllRainEffects()`, спільна з `EndPlay()`) і зупиняє `Tick()`/спавн
нових ефектів; будь-яке значення `> 0` — одразу перескановує світ (якщо перед
цим було `0`) і прокидається як User Parameter (Float) на кожен активний
`UNiagaraComponent` через `SetFloatParameter(TEXT("Intensity"),
RainIntensity)` (`ApplyIntensity()`, викликається і при зміні, і одразу при
спавні нового ефекту в `RescanAirplanes()`). **`NS_Rain` має експонувати User
Parameter з точно такою назвою (`Intensity`, тип Float) і використовувати
його** (типово — множником на Spawn Rate/Spawn Count) — інакше зміна значення
ні на що не вплине (окрім самого вкл/викл при переході через `0`, який працює
завжди, незалежно від User Parameter). Керується єдиним `SpinBoxRainIntensity`
у `UEnvironmentSectionWidget` (нема окремого чекбокса вкл/викл), персиститься
в `UEnvironmentSettingsSave::RainIntensity`. `UEnvironmentSectionWidget::
GetRainEffectManager()` лениво спавнить `ARainEffectManager`, якщо в рівні
його ще нема — так само, як `GetEnvironmentActorManager()`.

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
