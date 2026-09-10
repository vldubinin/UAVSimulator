# 07 — Запис, відтворення, режими

## Save-об'єкт сценарію — `UFlightScenarioSave`

`Save/FlightScenarioSave.h`. `USaveGame`.

| Поле | Тип | Опис |
|------|-----|------|
| `FlightFrames` | `TArray<FFlightFrame>` | Записані кадри |
| `AirplaneClassPath` | `FString` | Шлях класу записаного літака |
| `TotalFlightDuration` | `float` | Загальна тривалість (с) |

`FFlightFrame`: `Timestamp` (с), `Location : FVector`, `Rotation : FRotator`,
`ControlState : FControlInputState`.

Слот задається `ScenarioSlotName` на GameMode / `SaveSlotName` на компонентах
(за замовч. `"TargetScenario_1"`), `UserIndex = 0`.

## `UFlightRecorderComponent`

`Components/FlightRecorderComponent.h/.cpp`. Динамічно додається GameMode-ом у
режимі `RecordTarget`. Сам не тікає — пише по таймеру.

| Властивість | Замовч. | Опис |
|-------------|---------|------|
| `RecordInterval` | 0.1 с | Період запису кадру |
| `SaveSlotName` | `"TargetScenario_1"` | Слот `USaveGame` |

- **`StartRecording()`** — очищає буфер, ставить повторюваний таймер на
  `RecordFrame` кожні `RecordInterval`.
- **`RecordFrame()`** — `Timestamp += RecordInterval`; кадр = `GetActorLocation()`
  + `GetActorRotation()` + `FlightDynamics->GetControlState()`.
- **`StopRecordingAndSave()`** (з `EndPlay`) — очищає таймер, створює
  `UFlightScenarioSave` (`FlightFrames`, `TotalFlightDuration`,
  `AirplaneClassPath = Owner->GetClass()->GetPathName()`), `SaveGameToSlot`.

## `UFlightPlaybackComponent`

`Components/FlightPlaybackComponent.h/.cpp`. Динамічно додається GameMode-ом на
ціль у режимах `PlaybackAndTrack`, `PlaybackAndAutoTrack`, `Playback`. Тік
вимкнено на старті.

| Властивість | Замовч. | Опис |
|-------------|---------|------|
| `SaveSlotName` | `"TargetScenario_1"` | Слот сценарію |
| `PlaybackOffset` | `ZeroVector` | Зсув усієї траєкторії (для `PlaybackAndTrack`/`AutoTrack` = `InitialRotation.Vector() · TargetSpawnOffsetDistance`) |

**`StartPlayback()`** — завантажує сценарій; якщо порожній — вихід. Вмикає тік і
**вимикає конкурентні системи** на цільовому акторі саме в цьому порядку:
1. `UStaticMeshComponent::SetSimulatePhysics(false)` — солвер не бореться з
   трансформами відтворення;
2. `UFlightDynamicsComponent::SetComponentTickEnabled(false)` + `SetActive(false)`
   — сили не застосовуються.

> Примітка: у поточному коді `StartPlayback` вимикає лише mesh-фізику та
> `UFlightDynamicsComponent`. `UUAVPhysicsStateComponent` — субоб'єкт
> `UFlightDynamicsComponent` (тікає з його тіку), тож окремо його глушити не треба.

**`TickComponent`** — `CurrentPlaybackTime += DeltaTime`. Якщо перевищив
`TotalFlightDuration` — знімає тік, снапить на останній кадр (+ `PlaybackOffset`).
Інакше знаходить кадр B (перший із `Timestamp > CurrentPlaybackTime`), інтерполює
між A і B: `FMath::Lerp` для позиції, `FQuat::Slerp` для обертання;
`SetActorLocationAndRotation(InterpLocation + PlaybackOffset, InterpRotation)`.

## Режими симуляції (`ESimulatorMode`)

`Entity/SimulatorMode.h`. Обробляються в `AUAVSimulatorGameModeBase::StartSimulation()`
(див. [01-Architecture.md](01-Architecture.md#режими-симуляції-esimulatormode)).
`PlayerStart` задає стартовий трансформ; за відсутності — `FTransform::Identity`.
Актори — `SpawnActorDeferred` + тег ролі + `FinishSpawning`.

| Режим | DisplayName | Спавн | Опис |
|-------|-------------|-------|------|
| `RecordTarget` | Record Target | 1× `TargetAirplaneClass`, тег `Player` | `UFlightRecorderComponent` + `StartRecording()`. `USensorBusComponent::IsEnabledSensors` повертає `false` — шина мовчить під час запису |
| `PlaybackAndTrack` | Playback and Track | ціль (`Target`) + трекер (`Player`) | Трекер на `(InitialRotation, InitialLocation)`, ціль на `+ Offset`. Ціль: `UFlightPlaybackComponent` з offset. PC опановує трекер. Шина активна лише для `Player` |
| `PlaybackAndAutoTrack` | Playback and Auto Track | ціль (`Target`) + трекер (`AutoTracker`) | Як вище, + на трекері `AttitudeControl->CommandEndpoint = AttitudeCommandEndpoint`; `ActivateAutopilot()`. PC опановує трекер |
| `AutoTrack` | Auto Track | лише трекер на `PlayerStart`, тег `AutoTracker` | Без цілі й без відтворення. Автопілот активовано (уставки атитюду ззовні по ZMQ). PC опановує |
| `Playback` | Playback | лише ціль (`Target`) | `UFlightPlaybackComponent` без offset. PC нікого не опановує |
| `Free` | Free | 1× `TargetAirplaneClass`, тег `Player` | Вільний ручний політ. PC опановує |

Після спавну всіх акторів GameMode робить `UpdateCameraSettings()` +
`UpdateVisualSettings()` + `UpdateSensorSettings()` — broadcast делегатів
підсистеми, коли всі `AAirplane` уже підписані в `BeginPlay`.
