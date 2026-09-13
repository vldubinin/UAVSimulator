# Niagara VLM Flow Visualization — Setup Guide (Виправлена версія)

Цей документ описує покрокове налаштування Niagara System для фізично коректної візуалізації вихорового сліду (Vortex Lattice Method). Система використовує GPU для виконання рівняння Біо-Савара над десятками тисяч частинок.

---

## Крок 1 — Створення базової Niagara System

1. У Content Browser: **правий клік → FX → Niagara System**.
2. Обери **"New system from selected emitter(s)"** → знайди шаблон **"Fountain"**, додай його і натисни Finish.
3. Назви актив **`NS_VLMFlow`** і збережи його у своїй папці. Відкрий файл.

---

## Крок 2 — Переведення на GPU (Критично для продуктивності)

У панелі **Emitter** (центральне вікно):
1. Натисни на помаранчевий вузол **Emitter Properties**.
2. У панелі налаштувань (Selection) знайди **Sim Target** і зміни `CPUSim` на **`GPUCompute Sim`**.
3. Прокрути нижче до розділу **Bounds**.
4. Зміни `Calculate Bounds Mode` на **`Fixed`**.
5. У `Fixed Bounds` впишіть великі значення: Min `(-10000, -10000, -10000)`, Max `(10000, 10000, 10000)`. *(Це запобігає зникненню частинок, коли ви відвертаєте камеру)*.
6. Переконайся, що галочка **Local Space** знята.

---

## Крок 3 — Налаштування спавну "повітря"

У зеленій секції **Emitter Update**:
1. Видали модуль `Spawn Burst Instantaneous` (якщо є).
2. Залиш або додай `Spawn Rate` і встанови значення **`15000`** (або більше).

У помаранчевій секції **Particle Spawn**:
1. Натисни `+`, знайдіть і додай **Shape Location**.
2. У його налаштуваннях обери `Shape Primitive` -> **Box**.
3. Встанови `Box Size` на `(5.0, 1500.0, 500.0)` — це створить стіну частинок попереду літака.
4. Встанови `Offset` на `(500.0, 0.0, 0.0)` (відсуваємо спавн на 5 метрів вперед).
5. У модулі `Initialize Particle` встанови `Lifetime` на `Random` (від `2.0` до `5.0`), а `Sprite Size` на `5.0` або `10.0`.

---

## Крок 4 — Створення User Parameters (зв'язок з C++)

У панелі **Parameters** (User Parameters) додай через `+`:
1. **Array -> Vector**: Назви `WakePositions`.
2. **Array -> Float**: Назви `WakeGammas`.
3. **Float**: Назви `CoreRadius` і задай значення за замовчуванням `10.0`.

---

## Крок 5 — Модуль Custom HLSL (Біо-Савар)

У синій секції **Particle Update**:
1. Видали `Gravity Force` та `Drag`.
2. Натисни `+` -> додай **New Scratch Pad Module**. Відкриється редактор внизу.
3. У вузлі **Map Get** (вхідні дані) створи 4 змінні. Обов'язково в панелі їх налаштувань зміни **Namespace** на **`Module`**!
   - `ParticlePosition` (Тип: Position або Vector 3D)
   - `CoreRadius` (Тип: Float)
   - `WakePositions` (Тип: Vector 3D Array)
   - `WakeGammas` (Тип: Float Array)
4. У вузлі **Map Set** (вихідні дані) створи змінну. Переконайся, що її Namespace також стоїть **`Module`**!
   - `TotalInducedVelocity` (Тип: Vector 3D)
5. Додай між ними вузол **Custom HLSL**, з'єднай піни і встав цей виправлений код:

```hlsl
float3 InducedVelocity = float3(0.0f, 0.0f, 0.0f);

int ArraySize;
WakePositions.Length(ArraySize);

for (int i = 1; i < ArraySize; i++) {
    float3 P1; WakePositions.Get(i - 1, P1);
    float3 P2; WakePositions.Get(i, P2);
    
    float3 dl = P2 - P1;
    if (length(dl) > 500.0f) continue; // Розрив між крилами
    
    float3 r = ParticlePosition - P1;
    float3 crossProd = cross(dl, r);
    float rLen = length(r);
    float r3 = pow(rLen + CoreRadius, 3);
    
    float CurrentGamma; WakeGammas.Get(i, CurrentGamma);
    float GammaCm = CurrentGamma * 10000.0f; // SI (m^2/s) -> UE (cm^2/s)
    InducedVelocity += (GammaCm / 12.56637f) * (crossProd / r3);
}

TotalInducedVelocity = InducedVelocity;```

6. Натисни зелену кнопку Apply у Scratch Pad.

---

## Крок 6 — Прив'язка даних та застосування швидкості
У головному вікні емітера, на вашому модулі Scratch Pad, з'являться 4 поля входів. Прив'яжи їх:
- Particle Position -> Particles.Position
- Core Radius -> User.CoreRadius
- Wake Positions -> User.WakePositions
- Wake Gammas -> User.WakeGammas
Під цим модулем додай модуль Set New or Existing Parameter Variables.
Натисни +, обери Particles.Velocity.
У поле, що з'явиться, передай вихід з твого модуля: Scratchpad.TotalInducedVelocity.
Переконайся, що нижче стоїть Solve Forces and Velocity. Збережи NS_VLMFlow.

---

## Крок 7 — Підключення до літака
Відкрий Blueprint літака (BP_UAV).
Додай компонент Niagara Particle System і назви його FlowVisualizerComp.
В його налаштуваннях у Niagara System Asset обери NS_VLMFlow.
Відкрий вкладку Event Graph.
Від вузла Event BeginPlay витягни ноду Set Flow Visualizer (викликавши її з компонента, що рахує VLM, наприклад PhysicalAirplane або FlightDynamicsComponent).
Підключи FlowVisualizerComp у вхід цієї ноди.
Компілюй і запускай симуляцію. Частинки будуть закручуватися слідом за літаком згідно з математикою!