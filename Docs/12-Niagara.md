# 12 — Niagara-ефекти: вихоровий слід, дощ, вуличні вогні

Усі візуальні ефекти проєкту — це Niagara-системи в `Content/FX/`. Жодна не
керується з Blueprint-графа: вони отримують дані з C++ через **User Parameters**
(«контракт» між кодом і ассетом), а C++-власник сам спавнить/вмикає/вимикає
`UNiagaraComponent`.

| Ассет | Для чого | Хто спавнить і керує | Що передає C++ у систему | Sim Target |
|-------|----------|----------------------|--------------------------|------------|
| `NS_VLMFlow` | Візуалізація вихорового сліду за крилом і горизонтальним оперенням (налаштування руху повітря за методом вихрових ґраток) | `UAeroVisualizerComponent` (на літаку) | `WakePositions` (Vector3[]), `WakeGammas` (Float[]), `SurfaceSpan`, `ProbeHeight` (Float), `CoreRadius` (Float, дефолт ассету) | GPU |
| `NS_Rain` | Дощ над кожним літаком | `ARainEffectManager` (актор у рівні) | `Intensity` (Float) — **це і є Spawn Rate, частинок/с** | CPU |
| `NS_StreetLights` | Нічні вуличні вогні на Cesium-поверхні | `AStreetLightsManager` (актор у рівні) | `LightPositions` (Vector3[]), `TargetSpawnCount` (Int32), `Brightness` (Float) | GPU |

Кожен ассет — одна система з одним емітером і одним спрайтовим рендерером
(`NiagaraSpriteRendererProperties`). Ассети створено з шаблонів Niagara: «Fountain» для
`NS_VLMFlow` та `NS_StreetLights`, порожній емітер (`CompletelyEmpty`) для `NS_Rain`.
Матеріал спрайтів: `DefaultSpriteMaterial` у `NS_VLMFlow` і `NS_StreetLights`,
`/Game/Materials/Rain` у `NS_Rain`.

> Значення в цьому документі зчитано безпосередньо з ассетів (через `unreal-mcp`,
> `NiagaraToolset_System`), а не припущено; якщо ассет змінили — звіряйтеся з ним.

---

## Спільні принципи

**1. User Parameter = контракт.** Ім'я й тип параметра в ассеті мають *точно*
збігатися з тим, що викликає C++ (`SetFloatParameter(FName("Intensity"), …)` тощо).
Розбіжність не дає помилки — ефект просто не реагує. Тому перелік параметрів у
кожному розділі нижче — обов'язкова частина опису ефекту.

**2. Масиви даних — через Array Data Interface.** Позиції (вихорові вузли, вогні)
передаються `UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector/Float`
на User-параметр типу *Array → Vector3 / Float*. У шейдері читаються модулем
Niagara, що приймає масив на вхід.

**3. Обмеження Niagara, на які натрапили (важливо для будь-якого редагування):**
- **Custom Hlsl не може викликати функції Array DI на *User*-параметрі**
  (`User.X.Get(i)` / `.Length()`) — компіляція падає
  (`GetDuplicatedDataInterfaceCDOForClass failed`, `Cannot Set external constant`).
  Працює лише коли масив приходить у Scratch Pad-модуль як його власний вхід
  (`Module.WakePositions` у `NS_VLMFlow`) або через штатні dynamic input (`SelectVectorFromArray`
  у `NS_StreetLights`). Звичайні *значення* User-параметрів (`User.Brightness`) в HLSL
  читати можна.
- **User-параметри в графі read-only** — арифметику над ними (напр. `Довжина / Lifetime`)
  робить C++ і штовхає готове число окремим параметром (`TargetSpawnCount`).
- **`SelectVectorFromArray` бере ВИПАДКОВИЙ елемент**, а не за індексом частинки.
- **GPU-емітеру потрібні фіксовані Bounds**: Niagara не рахує межі GPU-частинок, тож без
  `Fixed Bounds` система відсікається за крихітною коробкою навколо компонента й
  зникає навіть у полі зору. У ассетах `NS_VLMFlow` і `NS_StreetLights` на емітері задано
  `Fixed` ±1000 см (системний `bFixedBounds` вимкнено); для `NS_StreetLights` цього
  замало (вогні розкидані на кілометри), тому C++ додатково викликає
  `SetSystemFixedBounds` навколо реальних позицій. `NS_Rain` — CPU-емітер з `Dynamic`
  bounds, фіксовані не потрібні.

**4. Час життя компонента.** Менеджери спавнять компоненти з `bAutoDestroy = false` і
самі знищують їх (`EndPlay`, зникнення літака); ефекти зациклені й самі не
«завершуються».

---

## `NS_VLMFlow` — вихоровий слід

### Навіщо

Показує, як крило й горизонтальне оперення «закручують» повітря: частинки-«повітря»
розлітаються під дією швидкості, що індукується вихровою стрічкою сліду
(закон Біо-Савара). Це **візуалізація**, а не джерело сил: сам слід і
індуктивний опір рахує `UFlightDynamicsComponent` (`02-FlightDynamics.md`), а Niagara лише
читає його результат.

### Як це працює

1. `UFlightDynamicsComponent` кожен тік будує вихоровий слід: для кожної поверхні
   обчислює циркуляцію `Γ`, додає в `GetVortexWakeLines()` вузли `FTrailingVortexNode`
   (позиція + `Γ`).
2. `UAeroVisualizerComponent` (`SceneComponent/AeroVisualizer/`, тік `TG_PostPhysics`
   з prerequisite на `UFlightDynamicsComponent`):
   - **`BeginPlay`** — для кожної `UAerodynamicSurfaceSC`, ім'я якої містить `Wing` або
     `TailHorizontal`, створює `UNiagaraComponent` (`FlowVisualizerSystem`, кріпиться до
     поверхні, `bAutoActivate = false`), рахує розмах `SpanCm` (з урахуванням масштабу
     меша й `Mirror`) і задає `SurfaceSpan`/`ProbeHeight` (крило: `SurfaceSpan = SpanCm`,
     `ProbeHeight = 2.0`; `TailHorizontal`: `SurfaceSpan = SpanCm × 1.5`,
     `ProbeHeight = 150`, компонент зсунуто на +100 см по X).
   - **`TickComponent` → `UpdateNiagaraWakeData()`** — «сплющує» всі лінії сліду в
     `WakePositions` + `WakeGammas`; після кожної лінії додає **сигнальний вузол**
     (`+Z 100000`, `Γ = 0`) — маркер кінця стрічки; штовхає в усі візуалізатори.
3. У ассеті GPU-емітер безперервно (22000 частинок/с) спавнить частинки в тонкому
   боксі на самій поверхні (`Box Size = (2, User.SurfaceSpan, User.ProbeHeight)`, тобто
   2 см завтовшки, шириною в розмах, висотою `ProbeHeight`); модуль на Scratch Pad для
   кожної частинки сумує внесок кожного відрізка сліду (Біо-Савар) і **записує результат у
   `Particles.Velocity`** (замінюючи початкову швидкість) — частинки закручуються слідом за
   вихором.
4. **Вмикання:** `AAirplane::RefreshConfigurations()` викликає
   `UAerodynamicSurfaceSC::SetNiagaraActive()` для кожної поверхні залежно від ролі
   літака й `UUAVSimulationSubsystem::bEnableVisualsForPlayer/Target` (`Player` і
   `AutoTracker` — за прапорцем гравця, `Target` — за прапорцем цілі). Прапорці
   штовхає `AUAVSimulatorGameModeBase::UpdateVisualSettings()` після спавну всіх
   літаків.

### Параметри

| Параметр | Тип | Звідки | Роль |
|----------|-----|--------|------|
| `WakePositions` | Array Vector3 | `UAeroVisualizerComponent`, щотіку | Вузли вихрової стрічки (світові см) |
| `WakeGammas` | Array Float | те саме | `Γ` у м²/с для кожного вузла |
| `SurfaceSpan` | Float (дефолт 0) | `BeginPlay` | Розмах поверхні (см) → `Y` розміру Box спавну |
| `ProbeHeight` | Float (дефолт 2) | `BeginPlay` | → `Z` розміру Box спавну (висота зони) |
| `CoreRadius` | Float (дефолт 10) | ассет | Радіус ядра вихору — згладжує сингулярність `1/r³` |

### Налаштування ассету крок за кроком

Потрібно лише при створенні/перебудові `NS_VLMFlow`; для звичайного використання
достатньо розділу «Підключення до літака».

**Крок 1 — базова система.** Content Browser → правий клік → *FX → Niagara System* →
*New system from selected emitter(s)* → шаблон **Fountain** → Finish → назвіть
`NS_VLMFlow`.

**Крок 2 — GPU (критично для продуктивності).** Емітер → *Emitter Properties* →
`Sim Target` = **GPUCompute Sim** → *Bounds*: `Calculate Bounds Mode` = **Fixed**,
`Fixed Bounds` Min `(-1000, -1000, -1000)` / Max `(1000, 1000, 1000)` (як у ассеті;
збільшуйте, якщо частинки зникають, віддаляючись від крила). `Local Space` — вимкнено.

**Крок 3 — модулі емітера (реальні значення ассету).**
- *Emitter Update*: `EmitterState` (Life Cycle = Self, Loop = Infinite), `SpawnRate` =
  **22000** (`Spawn Burst Instantaneous` видалено).
- *Particle Spawn*:
  - `Initialize Particle`: `Lifetime` = Random 2.0–5.0; `Mass` = Random 0.75–1.25;
    `Sprite Size` = Random Uniform 6–12; `Sprite Rotation` = Random 0–360°;
    `Color` = Direct Set `(0.075, 0.036, 1.0, 1.0)` (синій).
  - `Shape Location`: Shape Primitive = Box, `Box Size` = dynamic input **MakeVector**
    (`X = 2`, `Y = User.SurfaceSpan`, `Z = User.ProbeHeight`), `Offset` = 0.
  - `Add Velocity`: `Velocity Mode` = In Cone, `Cone Axis` = `(0,0,1)`, `Cone Angle` = 32°,
    `Velocity Speed` = dynamic input **RandomRangeFloat** 500–850.

**Крок 4 — User Parameters:** `WakePositions` (Array → Vector), `WakeGammas`
(Array → Float), `CoreRadius` (Float, 10.0).

**Крок 5 — Custom HLSL (Біо-Савар).** Порядок *Particle Update* в ассеті:
`Particle State` → `Scale Color` (Alpha з кривої) → `Solve Forces and Velocity` →
**`ScratchModule`** → `Set Variables` (`Particles.Velocity` ← `ScratchModule.TotalInducedVelocity`) →
`Solve Forces and Velocity` (ще раз, застосувати нову швидкість) → `Color` (з кривої).
`Gravity Force` і `Drag` відсутні. Створіть **New Scratch Pad Module**. У *Map Get* створити змінні з
Namespace **`Module`**: `ParticlePosition` (Position), `CoreRadius` (Float),
`WakePositions` (Vector3 Array), `WakeGammas` (Float Array). У *Map Set* — `TotalInducedVelocity`
(Vector3, Namespace `Module`). Між ними — вузол **Custom HLSL**:

```hlsl
float3 InducedVelocity = float3(0.0f, 0.0f, 0.0f);

int ArraySize;
WakePositions.Length(ArraySize);

for (int i = 1; i < ArraySize; i++) {
    float3 P1; WakePositions.Get(i - 1, P1);
    float3 P2; WakePositions.Get(i, P2);

    float3 dl = P2 - P1;
    if (length(dl) > 500.0f) continue; // розрив між крилами / сигнальний вузол

    float3 r = ParticlePosition - P1;
    float3 crossProd = cross(dl, r);
    float rLen = length(r);
    float r3 = pow(rLen + CoreRadius, 3);

    float CurrentGamma; WakeGammas.Get(i, CurrentGamma);
    float GammaCm = CurrentGamma * 10000.0f; // SI (м²/с) -> UE (см²/с)
    InducedVelocity += (GammaCm / 12.56637f) * (crossProd / r3);
}

TotalInducedVelocity = InducedVelocity;
```

Apply у Scratch Pad. (Тут `Array.Get/Length` працюють, бо масиви — це *входи модуля*, а
не User-параметри в HLSL; див. «Спільні принципи».)

**Крок 6 — прив'язка.** Входи модуля: `Particle Position` → `Particles.Position`,
`Core Radius` → `User.CoreRadius`, `Wake Positions` → `User.WakePositions`,
`Wake Gammas` → `User.WakeGammas`. Нижче — *Set New or Existing Parameter Variables*:
`Particles.Velocity` ← `ScratchModule.TotalInducedVelocity`; далі має стояти
**Solve Forces and Velocity**. Зберегти.

### Підключення до літака

Ручний Blueprint-wiring не потрібен — усе робить `UAeroVisualizerComponent`:
1. На Blueprint-літаку (напр. `Content/Airplanes/Cessna_172`) має бути компонент
   `UAeroVisualizerComponent`.
2. У ньому задайте `FlowVisualizerSystem` = `NS_VLMFlow`.
3. Імена `UAerodynamicSurfaceSC`, для яких потрібен слід, мають містити `Wing` або
   `TailHorizontal`.
4. Візуалізація вмикається прапорцями `bEnableVisualsForPlayer/Target` підсистеми
   (`UUAVSimulationSubsystem`).

---

## `NS_Rain` — дощ

### Навіщо

Атмосферний ефект: дощ над кожним літаком. Один поділюваний ассет; вмикається й
масштабується одним числом із меню.

### Як це працює

`ARainEffectManager` (`Actor/RainEffectManager.h/.cpp`, актор у рівні, по одному на
рівень) — детально в `13-Environment-Actors.md`:
- `RescanAirplanes()` (раз на `RescanInterval`, 1 с) знаходить усі `AAirplane`
  (`GetAllActorsOfClass`) і для кожного, якого ще нема в мапі `ActiveRainEffects`,
  спавнить окремий `UNiagaraComponent` (`SpawnSystemAtLocation`, **без** прикріплення,
  `bAutoDestroy = false`). Для знищених літаків компонент теж знищується.
- `UpdateRainPositions()` **щотіку** ставить компонент у
  `Airplane->GetActorLocation() + RainOffset` (дефолт `(0, 0, 2000)` см) з **нульовою
  ротацією** — дощ завжди над літаком і завжди падає вертикально вниз, що б не
  робив крен/тангаж.
- `SetRainIntensity(float)` (знизу обмежено нулем; дефолт 1) — єдине поле керування:
  `0` знищує всі ефекти й зупиняє спавн; `> 0` — прокидає значення в кожен
  активний компонент як User Parameter `Intensity` (Float) = Spawn Rate ассету
  (див. «Увага» нижче). Джерело в UI —
  `SpinBoxRainIntensity` секції *Environment*; зберігається в
  `UEnvironmentSettingsSave::RainIntensity`.

### Ассет (реальні значення)

CPU-емітер (`CPUSim`, шаблон `CompletelyEmpty`), **`Local Space` увімкнено**, bounds `Dynamic`,
`Life Cycle Mode = System`, Loop Infinite. Модулі:
- **`SpawnRate` ← Linked Variable `User.Intensity`** (дефолт змінної в ассеті — **100000**).
  Тобто `Intensity` — це не множник, а **кількість частинок за секунду**.
- `Initialize Particle`: `Lifetime` = 3 с; `Color` = `(0.80, 0.80, 1.0, 0.39)` (блідо-блакитний,
  напівпрозорий); `Sprite Size` = Non-Uniform `(0.5, 20)` — тонкі вертикальні «риски»-краплі.
- `Shape Location`: Box/Plane, `Box Size = (5000, 5000, 100)` см (майданчик 50×50 м, 1 м
  завтовшки), центр у компоненті (`Box Midpoint` 0.5).
- `Gravity Force` = `(0, 0, -980)` у світових координатах; `Solve Forces and Velocity`;
  `Particle State`.
- Рендерер: спрайти, матеріал **`/Game/Materials/Rain`**.

За `RainOffset.Z = 2000` см майданчик висить за 20 м над літаком; за 3 с життя
краплі під гравітацією падають ≈ 44 м, тобто проходять і крізь висоту літака.

> **Увага — семантика `Intensity`.** Оскільки `SpawnRate` = `User.Intensity` напряму, значення,
> яке C++ штовхає через `SetFloatParameter("Intensity", RainIntensity)`, і є щільність дощу
> в частинках/с. `SpinBoxRainIntensity` у `WBP_EnvironmentSection` налаштовано на
> діапазон **0–150000**; дефолт збереження (`UEnvironmentSettingsSave::RainIntensity`) і
> дефолт у C++ (`RainIntensity = 1.0`) дають лише ~1 краплю/с — практично без дощу, доки
> користувач не введе велике значення в спінбокс. Клемп `[0, 5]` у `UPROPERTY` — лише для
> редактора (`SetRainIntensity` обмежує знизу нулем). Якщо потрібна шкала «множник»,
> треба або помножити значення в C++ на базовий Spawn Rate, або пов'язати `Intensity` з
> множником у графі ассету.

---

## `NS_StreetLights` — нічні вуличні вогні

### Навіщо

Тисячі світлових точок уздовж периметра будівель на Cesium-поверхні — імітація
нічного міста. Один компонент, одна система, GPU-частинки замість актора чи компонента
на кожен ліхтар.

### Як це працює

`AStreetLightsManager` (`Actor/StreetLightsManager.h/.cpp`; повний опис — розділ
«Нічні вуличні вогні» в `13-Environment-Actors.md`):
1. `Scan()` щотіку читає результати сканерів із літаків — джерело обирає
   `EStreetLightsDataSource`: `Custom` (`UCustomSurroundingsScannerComponent`, готові контури
   будівель) або `Cesium` (`UCesiumSurroundingsScannerComponent`, випадковий
   прямокутник навколо точки влучання).
2. Для кожної нової будівлі `BuildLightsForObject()` розставляє вогні по периметру
   (крок `LightSpacingMeters` = 18 м, висота `LightHeightMeters` = 4 м) і кладе їх у
   `TrackedBuildingsMap`.
3. `RebuildNiagaraArrays()` (не частіше за `MinRebuildIntervalSeconds` = 2 с)
   збирає всі позиції в один масив і:
   - `SetNiagaraArrayVector("LightPositions", …)`;
   - `SetIntParameter("TargetSpawnCount", кількість)`;
   - `SetSystemFixedBounds(bounding-box усіх вогнів + 100 м)` і `SetAllowScalability(false)`;
   - `Activate(true)` (повний reset → новий burst на весь масив), лише поки `Brightness > 0`.
4. `SetBrightness(0..100)` (єдине поле керування): `0` → `Deactivate()`; `> 0` →
   `SetFloatParameter("Brightness", значення/100)` + `Activate()`. У UI —
   `SpinBoxStreetLightsBrightness` (0–100, персистується в
   `UEnvironmentSettingsSave::StreetLightsBrightness`).

### Параметри

| Параметр | Тип | Роль |
|----------|-----|------|
| `LightPositions` | Array Vector3 | Позиції всіх вогнів (світові см) |
| `TargetSpawnCount` | Int32 | Кількість вогнів = Spawn Count burst-у |
| `Brightness` | Float [0,1] | Множник кольору спрайта (`Brightness/100`) |

### Ассет (реальні значення)

GPU-емітер із шаблону Fountain (`GPUComputeSim`, `Local Space` вимкнено, емітерні
`Fixed Bounds` ±1000 см — у роботі їх перекриває `SetSystemFixedBounds` з C++), рендерер —
спрайти з `DefaultSpriteMaterial`. Модулі:
- *Emitter Update*: `EmitterState` (Life Cycle = Self, Loop = Infinite);
  `SpawnBurst_Instantaneous`, `Spawn Count` ← Linked Variable `User.TargetSpawnCount`
  (разовий burst, **не** безперервний `Spawn Rate` — щоб масив з'являвся одразу й вогні не
  «перетасовувались»).
- *Initialize Particle*: `Position Mode` = Direct Set, `Position` = dynamic input
  **`SelectVectorFromArray`** (`/Niagara/DynamicInputs/Arrays/`, масив ← `User.LightPositions`);
  `Lifetime Mode` = Direct Set, `Lifetime` = **86400** с (частинки живуть до наступного
  `Activate(true)`); `Color` = Direct Set, HLSL-вираз `float4(1.0, 0.75, 0.4, 1.0) * User.Brightness`;
  `Sprite Size` = Uniform **40**.
- *Particle Update*: лише `Particle State` (ні гравітації, ні руху, ні кривих кольору).

Змінні User: `Brightness` (Float, дефолт 1, опис «Global brightness [0,1]»), `LightPositions`
(Array Float3), `TargetSpawnCount` (Int32, дефолт 0). У ассеті ще лишились **невикористовувані
змінні, успадковані від `NS_VLMFlow`**: `CoreRadius`, `ProbeHeight`, `SurfaceSpan`, `WakeGammas`,
`WakePositions` — їх безпечно видалити (`RemoveUserVariables`).

Для фінального вигляду `DefaultSpriteMaterial` варто замінити на власний емісивний матеріал.

Наслідок вибору `SelectVectorFromArray`: він бере **випадковий** елемент, тож немає
гарантії «рівно один вогонь на позицію» — лише статистично рівномірне покриття
(усі позиції з часом отримують частинки). Точний індекс за `ExecIndex()` через
доступні dynamic input недосяжний (див. «Спільні принципи»).

---

## Як додати новий ефект

1. Створіть Niagara System у `Content/FX/`. GPU-емітер → відразу `Fixed Bounds`.
2. Визначте контракт: які User Parameters (ім'я + тип) дає C++.
3. Напишіть C++-власника (актор-менеджер у рівні або компонент на літаку) за одним із
   двох зразків: `ARainEffectManager` (компонент на кожен літак, слідує за ним) або
   `AStreetLightsManager` (один компонент на світ, масив даних).
4. Керуйте вмиканням одним числом/прапорцем з UI (`UEnvironmentSectionWidget` +
   `USaveGame`), не Blueprint-графом.
5. Опишіть параметри в цьому файлі.

## Поширені проблеми

| Симптом | Найімовірніша причина |
|---------|----------------------|
| Ефект є, але не реагує на керування | Ім'я/тип User Parameter в ассеті не збігається з C++ (`Intensity`, `Brightness`, `TargetSpawnCount`, `WakePositions`…) |
| GPU-частинки зникають, коли камера відвертається | Немає `Fixed Bounds` (для `NS_StreetLights` — не викликано `SetSystemFixedBounds`) |
| Вогні моргають | Занадто часті `Activate(true)`: збільште `MinRebuildIntervalSeconds`, не вмикайте `MaxTrackingDistanceMeters` без потреби |
| Компіляція Niagara падає з `GetDuplicatedDataInterfaceCDOForClass failed` | Виклик `Array.Get/Length` на User-параметрі в Custom HLSL — див. «Спільні принципи» |
| Слід (`NS_VLMFlow`) не видно | Не задано `FlowVisualizerSystem`; в іменах поверхонь немає `Wing`/`TailHorizontal`; вимкнено `bEnableVisualsForPlayer/Target` |
| Дощу немає | `RainSystem` не призначено на `ARainEffectManager`, або `RainIntensity == 0` |
| Дощ ледь помітний | `Intensity` = Spawn Rate (частинок/с): значення `1` — це ~1 краплю/с; задайте у спінбоксі тисячі (дефолт ассету — 100000) |
