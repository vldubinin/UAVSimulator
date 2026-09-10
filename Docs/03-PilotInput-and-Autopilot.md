# 03 — Керування пілота та автопілот

Усе керування літаком тече через один інтерфейс, дзеркалячи патерн сенсорної шини
(`IUAVSensorInterface` / `FSensorFrame`).

## Контракт: `IPilotInputSource`

`Interfaces/PilotInputSource.h`. Реалізується компонентами на `AAirplane`.

| Член | Опис |
|------|------|
| `bool bInputSourceEnabled` | Гейт. Default `false` — джерело інертне, доки його явно не ввімкнуть. Людські джерела вмикає координатор у `BeginPlay`; автопілот — сам у `ActivateAutopilot()` |
| `FName GetInputSourceId()` | Стабільний ID: `"keyboard"`, `"gamepad"`, `"autopilot"` |
| `int32 GetInputSourcePriority()` | Тир пріоритету. Координатор бере лише тир із найвищим числом |
| `void BindInput(UInputComponent*)` | Прив'язати власні осі/клавіші. Автопілот — порожньо |
| `bool GetPilotCommand(FPilotCommand&)` | Заповнити намір цього кадру; повертає `OutCommand.bHasInput` |

## Кадр наміру: `FPilotCommand`

`Structure/PilotCommand.h` — нормалізований намір від одного джерела за кадр.

| Поле | Діапазон | Сенс |
|------|----------|------|
| `Roll` | `[-1, 1]` | Намір елеронів (0 — нейтраль) |
| `Pitch` | `[-1, 1]` | Намір керма висоти. «Сирий» — інверсію робить координатор |
| `Yaw` | `[-1, 1]` | Намір керма напрямку |
| `ThrottleRate` | `[-1, 1]` | Інкрементний намір газу (додається до акумулятора × dt) |
| `ThrottleAbsolute` | `≥ 0` ⇒ джерело задає газ абсолютно `[0,1]`; `< 0` ⇒ не задає | напр. автопілот |
| `bHasInput` | `bool` | Чи джерело справді щось дає (інакше координатор його ігнорує) |

## Координатор: `UPilotInputComponent`

`Components/PilotInputComponent.h/.cpp`. CDO-компонент `AAirplane`. **Єдиний**, хто
викликає `UpdateAileron/Elevator/Rudder/ThrottleControl` на
`UFlightDynamicsComponent`.

**Властивості (EditAnywhere):**
- `InputSources : TArray<TObjectPtr<UActorComponent>>` — явний список джерел.
  Порожньо ⇒ авто-дискавер усіх `IPilotInputSource`-компонентів власника в
  `BeginPlay` (патерн `USensorBusComponent`).
- `bInvertPitch` (true, авіаційний стандарт) — спільна інверсія тангажу
  (властивість схеми керма планера). Застосовується **лише до людського тиру**
  (priority 0), ніколи до автопілота.
- `ThrottleModel : EPilotThrottleModel` — `Incremental` (вісь додає до
  акумулятора, газ утримується) або `Absolute` (вісь = положення газу:
  `out = вісь·0.5 + 0.5`).
- `ThrottleRampRate` (0.5) — частка ходу газу за секунду при повному відхиленні
  осі (Incremental). 0.5 ⇒ 0→1 за 2 с.
- `Throttle01` (`VisibleAnywhere`) — поточне положення газу `[0,1]`, утримується
  між кадрами. **Єдине місце стану газу пілота.** У `BeginPlay` ініціалізується
  з `FlightDynamics->TargetThrottle` (не збити стартовий газ із Blueprint).
- `bLogInputDebug` — лог активного тиру, зведених осей і газу.

**`ShouldDrive()`** — `true`, лише коли власник — локально керований `APawn` і є
`UFlightDynamicsComponent`.

**`TickComponent` (за кадр, якщо `ShouldDrive()`):**
1. **Збір.** Для кожного розв'язаного джерела: якщо `bInputSourceEnabled` і
   `GetPilotCommand(Cmd)` && `Cmd.bHasInput` — беруть його `Prio`. Тримають лише
   команди тиру з **найвищим** числом (`Active` reset при новому максимумі).
2. **Зведення.** У межах активного тиру осі **підсумовуються** й клампляться до
   `[-1, 1]`. `ThrottleRate` теж підсумовується; `ThrottleAbsolute` береться
   останній `≥ 0`.
3. **Інверсія тангажу** — `if (bInvertPitch && MaxPrio == 0) Pitch = -Pitch`.
   Через ексклюзивність тиру `Active` містить АБО людські джерела, АБО автопілот.
4. **Газ.** Якщо `ThrAbs ≥ 0` → `Throttle01 = Clamp(ThrAbs)`. Інакше за
   `ThrottleModel`: `Absolute` → `Clamp(ThrRate·0.5 + 0.5)`; `Incremental` →
   `Clamp(Throttle01 + ThrRate·ThrottleRampRate·DeltaTime)`.
5. **Запис.** `UpdateAileronControl(Roll, -Roll)` (диференціал: L/R протилежні),
   `UpdateElevatorControl(Pitch, Pitch)` (однаковий знак), `UpdateRudderControl(Yaw)`,
   `UpdateThrottleControl(Throttle01)`.

Тир 100 (активний автопілот) повністю перебиває тир 0 (клавіатура/джойстик).

## Джерело: клавіатура — `UKeyboardPilotInputComponent`

`Components/KeyboardPilotInputComponent.h/.cpp`. Тир 0, ID `"keyboard"`. Сам **не
тікає** — legacy `BindAxis`-делегати спрацьовують під час обробки вводу актора,
кешуючи сирі значення (`RawRoll/Pitch/Yaw/Throttle`, 0 коли клавішу відпущено).

`BindInput` прив'язує `KbdRoll`, `KbdPitch`, `KbdYaw`, `KbdThrottle`
(`Config/DefaultInput.ini`). `GetPilotCommand` — прямий прохід без deadzone/expo
(клавіатура вже дає чіткі ±1/0).

## Джерело: геймпад — `UGamepadPilotInputComponent`

`Components/GamepadPilotInputComponent.h/.cpp`. Тир 0, ID `"gamepad"`. Тестується
на **8BitDo SN30 Pro+ у режимі XInput** (утримати START+X при увімкненні). Сам не
тікає.

`BindInput` прив'язує `PadRoll`, `PadPitch`, `PadYaw`, `PadThrottle`.

**Форма осі** (`ShapeAxis`): deadzone → перемасштабування (край мертвої зони → 0,
повне відхилення досягає 1) → expo (`lerp(x, x³, Expo)`, знак збережено) →
sensitivity → `Clamp[-1,1]`. Газ — лише deadzone, без expo/чутливості
(накопичення робить координатор).

**Властивості:** `Roll/Pitch/Yaw/ThrottleDeadzone` (0.08 / 0.08 / 0.10 / 0.05),
`Roll/Pitch/YawExpo` (0.40 / 0.40 / 0.50), `Roll/Pitch/YawSensitivity` (1.0),
`bInvertRoll/Pitch/Yaw/Throttle` (полярність осей **цього пристрою**, не плутати з
`UPilotInputComponent::bInvertPitch`), `bLogRawAxes`.

## Розкладка керування — RC Mode 2

| Вісь | Клавіатура | Геймпад (XInput) |
|------|------------|------------------|
| Крен | `A` / `D` | права ручка X (`Gamepad_RightX`) |
| Тангаж | `Down` / `Up` | права ручка Y (`Gamepad_RightY`) |
| Курс (рискання) | `Left` / `Right` | ліва ручка X (`Gamepad_LeftX`) |
| Газ (інкрементно) | `S` / `W` | ліва ручка Y (`Gamepad_LeftY`) |

`Config/DefaultInput.ini` — осі `Kbd*` / `Pad*` (legacy input, без EnhancedInput,
без RawInput device config). `Gamepad_Special_Right` (Start) також відкриває меню
(`AUAVSimulatorPlayerController`).

---

# Автопілот — `UAttitudeControlComponent`

`Components/AttitudeControlComponent.h/.cpp`. CDO-компонент `AAirplane`. Одночасно:
- **`IPilotInputSource`** тир 100, ID `"autopilot"` — доки активований, повністю
  володіє літаком; `BindInput` порожній;
- **ZMQ PULL** приймач зовнішніх команд атитюду.

Запис у `UFlightDynamicsComponent` цей компонент **не** робить — лише **обчислює**
команди в `ComputeCommands()` і кешує в `Last*`; віддає їх координатору через
`GetPilotCommand()`.

## Активація

Компонент присутній на кожному літаку (CDO), але за замовчуванням
`bStartWithTickEnabled = false` — не тікає й не займає ZMQ-порт (усі літаки мають
однаковий `CommandEndpoint`). Активує лише явний виклик:

**`ActivateAutopilot()`** (викликає GameMode для Tracker-літака в
`PlaybackAndAutoTrack` / `AutoTrack`):
1. Знаходить `UFlightDynamicsComponent` (без нього — помилка, не активується).
2. `AddTickPrerequisiteActor(GetOwner())` — команди автопілота мають бути
   останнім записом за кадр (Blueprint-графа акторного тіку теж може писати в
   контроль-API).
3. `ZmqState = new FZmqPullState(CommandEndpoint)` → `Socket.bind(...)`;
   `SetComponentTickEnabled(true)`; `bInputSourceEnabled = true`.

`EndPlay` — `bInputSourceEnabled = false`, `delete ZmqState`.

## ZMQ-протокол уставок атитюду

- **Endpoint** (`CommandEndpoint`, EditAnywhere): `"tcp://*:5556"` за
  замовчуванням. GameMode встановлює його з `AttitudeCommandEndpoint`.
- Unreal-бік: `zmq.PULL` ← `bind("tcp://*:5556")`.
- Алгоритм-бік: `zmq.PUSH` → `connect("tcp://localhost:5556")`.
- **Формат** (JSON, UTF-8, plain send — не multipart):

  ```json
  {"command_type":"SET_ATTITUDE_TARGET","roll":0.0,"pitch":0.0,"yaw_rate":0.0,"thrust":0.5}
  ```

  `roll`, `pitch` — абсолютні кути у **радіанах** (світова система); `yaw_rate` —
  швидкість рискання рад/с; `thrust` — газ `[0,1]`.

**`PollCommands()`** (за тік) — `recv(ZMQ_DONTWAIT)` у циклі; парсить JSON
(використовуючи точну довжину `Msg.size()` — буфер не null-термінований); на
`SET_ATTITUDE_TARGET` оновлює `TargetRoll/Pitch/YawRate/Thrust`; невідомий
`command_type` — warning.

## Обчислення команд

**`ComputeCommands(DeltaTime)`** (за тік, після `PollCommands`):
- `CurrentRoll/Pitch` = `GetActorRotation()` у радіанах;
  `YawRateRadS` = `Mesh->GetPhysicsAngularVelocityInDegrees().Z` → рад/с;
- `Airspeed = FlightDynamics->GetAirspeed()` — вхід гейн-шедулінгу;
- `LastAileron = RollPid.Update(TargetRoll, CurrentRoll, dt, Airspeed)`;
  `LastElevator = PitchPid.Update(TargetPitch, CurrentPitch, dt, Airspeed)`;
  `LastRudder = YawRatePid.Update(TargetYawRate, YawRateRadS, dt, Airspeed)`;
  `LastThrust = TargetThrust`.

**`GetPilotCommand(OutCommand)`**: `Roll = LastAileron`, `Pitch = LastElevator`,
`Yaw = LastRudder`, `ThrottleRate = 0`, `ThrottleAbsolute = LastThrust`,
`bHasInput = (ZmqState != nullptr)` — активований ⇒ завжди володіє літаком.
Диференціал елеронів `(L, -L)` формує координатор.

## PID-регулятори (конфіг у Details-панелі Blueprint-класу)

`RollPid`, `PitchPid`, `YawRatePid` — окремі `FPidController` на кожен канал.
Кожен Blueprint-нащадок `AAirplane` має власну збережувану конфігурацію.

Стартові значення (з конструктора `UAttitudeControlComponent`, фінальні
підбираються в PIE):
- `RollPid`: `Kp=2.0`, `Kd=0.4`, `bIsAngularError=true` — крен це кут (подвійний
  інтегратор, чистий П дає незгасаючі коливання; Kd додає демпфування).
- `PitchPid`: `Kp=2.0`, `Kd=0.4`, `bIsAngularError=true`.
- `YawRatePid`: `Kp=1.0`, `bIsAngularError=false` — регулює **швидкість**
  (одинарний інтегратор), чистого П достатньо, обгортання не потрібне.

`bLogAttitudeDebug` — лог уставка/поточне/вихід кожного PID.

## `FPidController`

`Entity/PidController.h/.cpp` — `USTRUCT(BlueprintType)`. `Update(Setpoint,
Measurement, DeltaTime, ScheduleInput = 0) → [OutputMin, OutputMax]`.

| Властивість | Замовч. | Роль |
|-------------|---------|------|
| `Kp`, `Ki`, `Kd` | 1 / 0 / 0 | Коефіцієнти |
| `OutputMin` / `OutputMax` | −1 / 1 | Межі виходу |
| `IntegralClamp` | 1 | Анти-windup: межа **вкладу** I-складової у вихід (в одиницях виходу) |
| `DerivativeFilterTimeConstant` | 0.05 | Стала часу НЧ-фільтра похідної (с); 0 — вимкнено |
| `bDerivativeOnMeasurement` | true | Диференціювати вимірювання, а не похибку (уникає «кидка» при зміні уставки) |
| `bIsAngularError` | false | Похибка — кут через ±180° (обгортка через `FMath::UnwindRadians`) |
| `bEnabled` | true | Вимикання каналу без втрати стану (вихід форсовано 0, стан оновлюється) |
| `bInvertOutput` | false | Інверсія суми P+I+D до клемпування — якщо фізична відповідь літака протилежна очікуваній |
| `GainScheduleCurve : FRuntimeFloatCurve` | порожня | Гейн-шедулінг: множник Kp/Ki/Kd від `ScheduleInput` (типово повітряна швидкість — аеровідгук ~ V²). Порожня крива = множник 1 |

**Алгоритм `Update`:**
- `DeltaTime ≤ 0` → повертає `LastOutput`.
- `ScheduleMultiplier` = `GainScheduleCurve.Eval(ScheduleInput)` (або 1);
  `EffectiveKp/Ki/Kd = K · множник`.
- `Error = Setpoint − Measurement` (+ `UnwindRadians` якщо кутова).
- `!bEnabled` → стежить за станом, повертає 0.
- `PTerm = EffectiveKp · Error`.
- Анти-windup: `Integral += Error·dt`; `ITerm = Clamp(EffectiveKi·Integral,
  ±IntegralClamp)`; `Integral` перераховується назад із клампнутого `ITerm`
  (щоб межа не «пливла» разом із гейн-шедулінгом).
- `DTerm` (лише якщо `bHasPrevious`): `RawDerivative = bDerivativeOnMeasurement
  ? −ΔMeasurement/dt : ΔError/dt`; НЧ-фільтр
  `FilteredDerivative += α·(Raw − Filtered)`, `α = dt/(τ + dt)`;
  `DTerm = EffectiveKd · FilteredDerivative`. Перший виклик навмисно лишає
  `DTerm = 0` (уникає спотвореного кидка на старті).
- `RawOutput = PTerm + ITerm + DTerm` (× −1 якщо `bInvertOutput`) →
  `Clamp(OutputMin, OutputMax)`.

Стан для спостереження (`VisibleAnywhere`): `Integral`, `PreviousError`,
`PreviousMeasurement`, `FilteredDerivative`, `LastOutput`, `bHasPrevious`.
`Reset()` скидає все.
