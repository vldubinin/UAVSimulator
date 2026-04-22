# Niagara VLM Flow Visualization — Setup Guide

Цей документ описує покроковe налаштування Niagara System для візуалізації вихорового сліду на основі даних, що передаються з `FlightDynamicsComponent` кожен тік.

C++ сторона вже готова: масиви `WakePositions` і `WakeGammas` передаються через `UNiagaraDataInterfaceArrayFunctionLibrary` у `SendWakeDataToNiagara()`.

---

## Крок 1 — Створення Niagara System

1. У Content Browser: **правий клік → FX → Niagara System**
2. Обери **"New system from selected emitter(s)"** → знайди **"Fountain"** або **"Simple Sprite Burst"** як базу (потім замінимо рендерер на стрічки)
3. Назви актив **`NS_VLMFlow`** і збережи у `Content/FX/`
4. Відкрий подвійним кліком — відкриється **Niagara Editor**

---

## Крок 2 — Налаштування емітера

У панелі **Emitter** (ліва колонка):

1. **Emitter Properties → Sim Target**: залиш `CPUSim` — дані приходять з CPU кожен тік
2. **Spawn Rate**: встанови `0` — частинки не спавняться автоматично; позиції ми задаємо вручну через масив
3. **Spawn Burst Instantaneous**: також `0`

> Ми керуватимемо кількістю частинок вручну через кількість вузлів у `WakePositions`.

---

## Крок 3 — Додавання User Parameters (змінні-масиви)

У панелі **Parameters** (права колонка) → вкладка **User**:

1. Натисни **"+"** → **Array → Vector**
   - Назва: `WakePositions`
   - Тип: `Vector Array` (`float3[]`)

2. Натисни **"+"** → **Array → Float**
   - Назва: `WakeGammas`
   - Тип: `Float Array` (`float[]`)

> **Важливо:** імена мають збігатися **точно** з рядками у `SendWakeDataToNiagara()`:
> ```cpp
> FName("WakePositions")
> FName("WakeGammas")
> ```

---

## Крок 4 — Particle Spawn: ініціалізація позицій

У секції **Particle Spawn** додай модуль **"Initialize Particle"**:

- **Position**: прив'яжи до `User.WakePositions[Particles.UniqueID]`
  - Для цього: клікни на поле Position → **"Link Input"** → `User.WakePositions` → використай індекс `Particles.UniqueID`

Це розмістить кожну частинку точно у відповідному вузлі сліду при народженні.

---

## Крок 5 — Particle Update: Custom HLSL модуль (Biot-Savart)

У секції **Particle Update**:

1. Натисни **"+"** → **"Scratch Pad Module"** або **"Custom HLSL"**
2. Назви модуль `BiotSavart_InducedVelocity`

### Вхідні параметри модуля

Додай у **Inputs** цього модуля:

| Назва | Тип |
|---|---|
| `ParticlePosition` | `Vector (float3)` |
| `ArraySize` | `Int` |
| `WakePositions` | `Array<Vector>` |
| `WakeGammas` | `Array<Float>` |
| `CoreRadius` | `Float` |

Прив'яжи:
- `ParticlePosition` → `Particles.Position`
- `ArraySize` → `length(User.WakePositions)` або через окремий User Int параметр
- `WakePositions` → `User.WakePositions`
- `WakeGammas` → `User.WakeGammas`
- `CoreRadius` → константа `10.0` (або User Float параметр для тюнінгу)

### HLSL код

```hlsl
float3 TotalInducedVelocity = float3(0.0, 0.0, 0.0);

for (int i = 1; i < ArraySize; i++)
{
    float3 PosA = WakePositions[i - 1];
    float3 PosB = WakePositions[i];

    // Захист від з'єднання сегментів різних ліній сліду (різні крила/хвіст)
    // у плоскому масиві вони розташовані підряд, але фізично далеко одне від одного
    if (distance(PosB, PosA) > 500.0)
    {
        continue;
    }

    // Вектор сегменту вихорової нитки
    float3 dl = PosB - PosA;

    // Вектор від початку сегменту до точки спостереження
    float3 r = ParticlePosition - PosA;

    float3 crossProd = cross(dl, r);
    float  rLen      = length(r);

    // Знаменник з CoreRadius для уникнення сингулярності при rLen → 0
    float r3 = pow(rLen + CoreRadius, 3.0);

    // Закон Біо-Савара: dV = (Γ / 4π) * (dl × r) / |r|³
    // 1 / (4π) ≈ 1 / 12.56637
    TotalInducedVelocity += (WakeGammas[i] / 12.56637) * (crossProd / r3);
}
```

### Вихідний параметр

Додай у **Outputs** модуля:

| Назва | Тип |
|---|---|
| `InducedVelocity` | `Vector (float3)` |

Останній рядок у HLSL:
```hlsl
InducedVelocity = TotalInducedVelocity;
```

---

## Крок 6 — Застосування індукованої швидкості до частинок

Після модуля `BiotSavart_InducedVelocity` у секції **Particle Update** додай ще один **"Custom HLSL"** або використай вбудований **"Add Velocity"**:

**Варіант A — через вбудований модуль "Add Velocity":**
- Додай **Add Velocity**
- У поле **Velocity** встав вивід `InducedVelocity` від попереднього модуля

**Варіант B — через Custom HLSL (точніший контроль):**
```hlsl
// ScaleFactor дозволяє тюнити силу візуального ефекту незалежно від фізики
Particles.Velocity += InducedVelocity * ScaleFactor;
```
де `ScaleFactor` — User Float параметр (рекомендоване початкове значення: `0.1`).

---

## Крок 7 — Рендерер: Ribbon (стрічки)

Щоб слід виглядав як суцільні лінії потоку, а не окремі спрайти:

1. У **Render** секції: видали **Sprite Renderer**
2. Додай **Ribbon Renderer**
3. Налаштуй:
   - **Ribbon Link Order**: `Particles.UniqueID`
   - **Width**: `2.0–5.0` (або прив'яжи до `abs(User.WakeGammas[i])` для варіації товщини за силою вихору)
   - **Material**: створи простий матеріал з `Unlit + Translucent`, колір прив'яжи до градієнта за Gamma (синій → червоний)

---

## Крок 8 — Підключення у Blueprint літака (BP_UAV)

1. Відкрий **BP_UAV** у Blueprint Editor
2. У панелі **Components** знайди **`FlightDynamicsComponent`**
3. У **Details** панелі → секція **"Aircraft | VLM"** → поле **`Flow Visualizer`**
4. Натисни **"+"** праворуч → додай новий компонент **Niagara System**:
   - **Niagara System Asset**: обери `NS_VLMFlow`
5. Перетягни цей новий **NiagaraComponent** у слот **`Flow Visualizer`**

**Або через Blueprint вузли (у `BeginPlay` графі BP_UAV):**
```
[BeginPlay]
    ↓
[Get FlightDynamicsComponent]
    ↓
[Set Flow Visualizer] ← [Get Component by Class: NiagaraComponent]
```

---

## Підсумок потоку даних

```
FlightDynamicsComponent::TickComponent()
    └── UpdateVortexWake()         — оновлює VortexWakeLines
    └── SendWakeDataToNiagara()    — flatten → SetNiagaraArrayVector / SetNiagaraArrayFloat
            ↓
    NS_VLMFlow (NiagaraComponent на літаку)
        └── User.WakePositions[]   — позиції вузлів сліду
        └── User.WakeGammas[]      — циркуляція Γ кожного вузла
            ↓
    Particle Update: BiotSavart_InducedVelocity (HLSL)
        └── Particles.Velocity += InducedVelocity * ScaleFactor
            ↓
    Ribbon Renderer                — видима стрічка потоку у viewport
```

---

## Параметри для тюнінгу (User Parameters у NS_VLMFlow)

| Параметр | Тип | Рекомендоване значення | Ефект |
|---|---|---|---|
| `CoreRadius` | Float | `10.0` | Зменшення → гостріші вихори, збільшення → плавніший ефект |
| `ScaleFactor` | Float | `0.1` | Масштаб візуального впливу на швидкість частинок |
| `RibbonWidth` | Float | `3.0` | Товщина стрічки сліду |
