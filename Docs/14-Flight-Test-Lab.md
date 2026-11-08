# 14 — Стенд віртуальних льотних випробувань (Flight Test Lab) з LLM-аналізом

Наскрізний конвеєр: **умови тесту в Unreal → алгоритм керування користувача на Python →
запис повної телеметрії → автоматичний аналіз Claude → звіт про слабкі місця й рекомендації.**

```
 Unreal (UAVSimulator)                               Python-стенд  Tools/TestingPlatform/flight_lab  (пакет uavlab)
 ┌───────────────────────────────┐                   ┌───────────────────────────────────────────┐
 │ USensorBusComponent  PUB :5555│──сенсори+truth──▶ │ Bench: цикл з частотою rate_hz            │
 │   + UFlightTruthComponent     │                   │   Observation -> Controller.step(obs)     │
 │     (топік flight_truth)      │                   │   -> Command                              │
 │ UAttitudeControlComponent     │◀──SET_ATTITUDE──  │   SessionRecorder (sessions/<id>/)        │
 │                    PULL :5556 │                   │   StopCriteria                            │
 │ UTestSessionSubsystem         │◀─REQ/REP─────────▶│ після зупинки:                            │
 │   REP :5557 (керування)       │                   │   metrics -> plots -> pack -> Claude      │
 │   PUB :5558 (події сесії)     │──session_*/event─▶│   -> report.json + report.html            │
 └───────────────────────────────┘                   └───────────────────────────────────────────┘
```

## Робочий процес користувача

1. **Умови тесту — у меню Unreal** (як завжди): місце (Cesium georeference), оточення
   (час доби, дощ, туман, хмари, вітер, зони РЕБ), сенсори (секція *Sensors*), режим.
   Стенд нічого з цього не дублює — він отримує все в **маніфесті** (див. нижче).
2. **Алгоритм** — один `.py` файл з підкласом `uavlab.Controller` (або функцією `step(obs)`).
3. **Конфіг тесту** (YAML, опційно): назва, **мета тесту словами** (`objective` — ключовий вхід
   для LLM), режим старту, критерії зупинки, параметри алгоритму.
4. **Запуск**: `uavlab run my_algo.py -c my_test.yaml`.
   - `start: auto` — стенд сам стартує симуляцію (команда `START`, меню ховається);
   - `start: wait` — стенд чекає, доки користувач натисне **Start** у меню.
5. **Зупинка** — з будь-якого боку: `Q` / Start на геймпаді в Unreal, критерій стенда
   (ліміт часу, зіткнення, дистанція, висота, втрата телеметрії, виняток алгоритму,
   `is_done()`), або `Ctrl+C`. Стенд у будь-якому разі шле `STOP` (меню повертається).
6. **Аналіз** автоматично: метрики → графіки → пакет → Claude → `report.html`.
   Повторно (без Unreal): `uavlab analyze sessions/<id>`.

## Встановлення

```bash
cd Tools/TestingPlatform/flight_lab
pip install -e .                 # pyzmq, numpy, pandas, matplotlib, pyyaml, anthropic
pip install -e .[camera]         # + opencv-python, якщо алгоритм читає obs.camera
```

Ключ Claude: змінна `ANTHROPIC_API_KEY` (або профіль `ant auth login`). Без ключа стенд
усе одно записує сесію і будує звіт з метриками/графіками, а LLM-частину можна запустити
пізніше `uavlab analyze`.

## Підключення алгоритму

```python
from uavlab import Controller, Command

class MyAutopilot(Controller):
    rate_hz = 20                                  # опційно; дефолт — control_rate_hz з конфігу

    def on_start(self, ctx):                      # опційно
        self.kp = ctx.params.get("kp", 0.3)       # controller_params з YAML
        # ctx.manifest — повні умови тесту з Unreal

    def step(self, obs) -> Command:               # обов'язково
        err = 120.0 - obs.truth.alt_m             # або obs.get("altimeter")["altitude_m"]
        self.ctx.log("alt_error_m", err)          # внутрішня змінна -> телеметрія + LLM
        return Command(roll_deg=0, pitch_deg=self.kp * err, yaw_rate_deg_s=0, thrust=0.7)

    def is_done(self, obs) -> bool:               # опційно: мету досягнуто -> тест зупиняється
        return False
```

- `Command` — **градуси** (крен/тангаж — абсолютні уставки, `yaw_rate_deg_s` — °/с), `thrust` — [0,1].
  Переведення в радіани для `SET_ATTITUDE_TARGET` робить стенд (`transport.build_attitude_command`).
  Уставки відпрацьовує внутрішній PID `UAttitudeControlComponent` (див. [03](03-PilotInput-and-Autopilot.md)).
- `step()` може повернути `None` — нова команда не шлеться, автопілот тримає попередню уставку.
- Також приймаються `dict` з полями `Command` або кортеж `(roll, pitch, yaw_rate, thrust)`.
- Файл з функціями замість класу: `def step(obs)` або `def step(obs, ctx)` + опційні
  `on_start(ctx)`, `on_stop(ctx)`, `is_done(obs)`, константа `RATE_HZ`.
- `ctx.event("waypoint_reached", index=2)` — подія алгоритму в `events.jsonl` і в аналіз.
- Виняток у `step()` логується з traceback; після `stop.max_step_errors` тест зупиняється.

### `Observation`

| Поле | Опис |
|------|------|
| `obs.t` | симуляційний час (с) останнього пакета шини |
| `obs.truth` | `Truth` (ground truth `flight_truth`) або `None` при `use_truth: false` |
| `obs.sensors` / `obs.get(topic)` | `{topic: payload}`: JSON-топіки — `dict`, `camera`/`segmentation_mask` — JPEG `bytes` |
| `obs.camera` | декодований кадр BGR (numpy) — потребує `opencv-python` |
| `obs.sensor_times` | `{topic: час пакета}` — для оцінки «віку» даних |

`Truth`: `lat`, `lon`, `alt_m`, `roll_deg`, `pitch_deg`, `yaw_deg`, `heading_deg`, `course_deg`,
`yaw_rate_dps`, `airspeed_mps`, `ground_speed_mps`, `vertical_speed_mps`, `aoa_deg`, `throttle`,
`position_m`; повний словник — `truth.raw`.

**`use_truth: false`** — «чесний» тест: алгоритм бачить лише бортові сенсори (увімкнені в
меню), але `flight_truth` однаково пишеться й іде в аналіз.

## Конфіг тесту (YAML)

Повний перелік дефолтів — `uavlab/config.py` (`DEFAULTS`). Основне:

```yaml
name: "Утримання висоти при боковому вітрі"
objective: "Тримати 120 м ±5 м, курс 90° ±3°, 3 хв, без виходу на AoA > 15°"
start: auto                # auto | wait
mode: AutoTrack            # AutoTrack | PlaybackAndAutoTrack (режими з автопілотом)
scenario_slot: null        # слот траєкторії цілі для PlaybackAndAutoTrack
control_rate_hz: 20
use_truth: true
controller_class: null     # якщо у файлі кілька підкласів Controller
controller_params: {kp: 0.3}
stop:
  time_limit_s: 180        # симуляційний час
  on_crash: true
  max_distance_m: null
  min_altitude_m: null
  telemetry_timeout_s: 5
  max_step_errors: 1
  progress_key: null       # змінна ctx.log, що має зростати (напр. waypoint_index)
  no_progress_s: null      # ... не зростала стільки секунд -> зупинка "no_progress" (кружляння, зависання)
record:
  sessions_dir: sessions
  sensors_max_hz: 10
  sensor_topics: null      # які топіки писати в sensors.jsonl (null — усі)
  max_sensor_payload_kb: 16  # більші payload'и не пишуться (custom_objects ~75 КБ/кадр -> сотні МБ за тест)
  extra_sources: []        # модулі алгоритму з інших тек — щоб LLM бачив увесь код
analysis:
  enabled: true
  model: claude-opus-5-5
  effort: high
  language: uk
```

Готові приклади: `flight_lab/examples/hold_altitude.py` + `test_configs/hold_altitude.yaml`,
`examples/waypoints_gps.py` + `test_configs/waypoints_gps.yaml` (адаптер наявного
`custom_objects_dataset/gps_autopilote/autopilote.py`, позиція — з сенсора `drone_geo_position`).

## Unreal-сторона

### `UTestSessionSubsystem` (`Subsystem/TestSessionSubsystem.h/.cpp`)

`UTickableWorldSubsystem`, лише Game/PIE світи. Сокети біндяться в `OnWorldBeginPlay`, опитуються
щотіку з `ZMQ_DONTWAIT` (ігровий потік не блокується).

**REP `tcp://*:5557`** — запит/відповідь JSON, відповідь завжди `{"ok": bool, ...}`:

| Запит | Відповідь / дія |
|-------|-----------------|
| `{"command":"PING"}` | `{"ok":true,"protocol":1}` |
| `{"command":"STATUS"}` | `running`, `session_id`, `sim_time`, `mode` |
| `{"command":"GET_MANIFEST"}` | `{"manifest":{...}}` — під час сесії той, що зібраний на старті |
| `{"command":"START","mode":"AutoTrack","scenario_slot":"..."}` | `mode`/`scenario_slot` опційні; `AUAVSimulatorPlayerController::StartSimulationAndHideMenu()`; відповідь `session_id`, `mode`, `autopilot_active` |
| `{"command":"STOP","reason":"time_limit"}` | `StopSimulationAndShowMenu()` (те саме, що `Q`); причина в `session_end` — `bench:<reason>` |
| `{"command":"SET_AUTOPILOT_PID","pid_overrides":{"roll":{"kp":3}}}` | PID автопілота всіх активних літаків **на час сесії** (Blueprint не змінюється); оновлює маніфест, подія `pid_changed`; відповідь `applied_to` |

`START` також приймає `"pid_overrides"` — вони застосовуються в `NotifySimulationStarted()` **до** побудови
маніфесту. Поля осі (`roll` / `pitch` / `yaw_rate`): `kp`, `ki`, `kd`, `integral_clamp`, `derivative_filter_s`,
`invert_output` (bool), `gain_schedule` (`"keep"` | `"disable"` — `FPidController::DisableGainSchedule()`
обнуляє криву лише для екземпляра). Після зміни викликається `FPidController::Reset()`. У конфігу стенда —
секція `autopilot_pid`. Маніфест містить `gain_schedule_active` по осях, `flight_truth` —
`autopilot.gain_schedule_multiplier` (множник на поточній швидкості: ефективний Kp = Kp × множник).

**PUB `tcp://*:5558`** — односкладові JSON-події:

- `{"type":"session_start","session_id","sim_time","manifest"}`
- `{"type":"session_end","session_id","reason","sim_time","duration_s"}` — `reason`: `user` (Q / Start
  на геймпаді) або `bench:<причина>`
- `{"type":"event","kind":"collision","actor","other_actor","other_class","impulse_ns","speed_mps","sim_time"}` —
  `OnComponentHit` фізичного меша кожного `AAirplane` (cooldown 0.5 с). Рішення зупиняти тест
  приймає стенд (`stop.on_crash`).

**Хуки:** `AUAVSimulatorGameModeBase::StartSimulation()` → (спавн у `StartSimulationImpl()`) →
`NotifySimulationStarted()`; `StopSimulation()` → `NotifySimulationStopped()` **до** знищення
літаків. Тому старт/стоп з меню й зі стенда проходять одним шляхом.

`session_id` = `YYYYMMDD_HHMMSS_<ESimulatorMode>`; тека сесії стенда — `<session_id>_<файл алгоритму>`.

### Маніфест умов тесту

Збирається `BuildManifest()` з наявних джерел (нічого не дублюється):

| Ключ | Джерело |
|------|---------|
| `level`, `simulation` | рівень; `ESimulatorMode`, слот сценарію, офсет цілі, endpoint команд, класи літаків, режим бортової камери |
| `physics` | `UPhysicsSettings`: substepping, max substeps, max substep dt |
| `location` | `ACesiumGeoreference` origin lat/lon/height |
| `menu_settings` | **усі** `USaveGame`-слоти секцій меню (`ScenarioSettings`, `SensorSettings`, `EnvironmentSettings`, `GlobalSettings`) через `FJsonObjectConverter` — час доби, швидкість часу, дощ, туман, хмари, зорі, вуличні вогні тощо |
| `sensor_flags` | фактично застосовані прапорці `UUAVSimulationSubsystem` (`SensorsMode` + 13 сенсорів) |
| `ew_zones`, `wind_vectors` | `UUAVSimulationSubsystem::EWZones` / `WindVectors`: позиція, радіус, швидкість, напрямок |
| `aircraft[]` | клас, теги ролі, масштаб, `CalibrationSettings`, маса, розмах, параметри двигуна, **PID автопілота** (`RollPid`/`PitchPid`/`YawRatePid` повністю), сенсорна шина (endpoint, rate), перелік сенсорів з `enabled` |

PID внутрішнього контуру в маніфесті потрібні LLM, щоб відрізняти проблему алгоритму
користувача від налаштування автопілота.

### `UFlightTruthComponent` — топік `flight_truth`

CDO на `AAirplane` (`Components/FlightTruthComponent.h/.cpp`), `IUAVSensorInterface`, публікується
через наявний `USensorBusComponent`. `TG_PostPhysics` + тік-пререквізит на `UFlightDynamicsComponent`.
Увімкнення (`AAirplane::RefreshSensorSettings`): `bEnableSensorFlightTruth && (bSensorsActive ||
AutoTracker)` — автопілотований літак публікує truth незалежно від `SensorsMode`; окремого
чекбокса в меню немає (`UUAVSimulationSubsystem::bEnableSensorFlightTruth`, дефолт `true`).

Payload:

```json
{
  "sim_time": 12.34,
  "position_m": {"x": 0, "y": 0, "z": 0},
  "geo": {"latitude": 50.04, "longitude": 36.28, "altitude_m": 120.5},
  "velocity_enu_mps": {"east": 0, "north": 40, "up": 0.1},
  "heading_deg": 2.1, "course_deg": 1.8, "ground_speed_mps": 40.1, "vertical_speed_mps": 0.1,
  "velocity_mps": {"x": 0, "y": 0, "z": 0},
  "attitude_deg": {"roll": 0, "pitch": 2, "yaw": 90},
  "body_rates_dps": {"p": 0, "q": 0, "r": 0},
  "yaw_rate_world_dps": 0.0,
  "airspeed_mps": 40.2, "aoa_deg": 3.1, "sideslip_deg": 0.2, "load_factor_g": 1.0,
  "throttle": 0.7, "thrust_n": 410.0,
  "autopilot": {
    "active": true,
    "target": {"roll_deg": 0, "pitch_deg": 2, "yaw_rate_dps": 0, "thrust": 0.7},
    "pid_output": {"aileron": 0.01, "elevator": -0.05, "rudder": 0.0},
    "last_command_age_s": 0.05, "commands_received": 240
  },
  "environment": {"wind_mps": {"x": 0, "y": 0, "z": 0}, "wind_speed_mps": 0, "ew_intensity": 0}
}
```

- `heading_deg`/`course_deg`/`velocity_enu_mps` рахуються через геореференс (ENU), тож не
  залежать від орієнтації рівня. `course_deg` є лише при `ground_speed_mps > 0.5`.
- `yaw_rate_world_dps` — той самий сигнал, який порівнює `YawRatePid` (світова Z).
- `autopilot.*` — нові геттери `UAttitudeControlComponent` (`GetTarget*`, `GetLast*Cmd`,
  `GetLastCommandWorldTime`, `GetReceivedCommandCount`, `IsAutopilotActive`).
- `load_factor_g` — `(a + g)·Up / g` з різниці швидкостей між тіками.

## Жива карта: `--map`

```powershell
uavlab run examples/waypoints_gps.py -c test_configs/waypoints_gps.yaml --map
```

Окреме вікно OpenStreetMap (`uavlab/livemap.py`, пакет `tkintermapview`: `pip install -e .[map]`):

- **літак** — синій маркер з висотою / швидкістю / курсом, стрілка курсу (250 м) і синій трек;
- **точки маршруту** з `controller_params.waypoints` (`[[lat, lon, alt], ...]`) з колами радіуса захоплення
  (`controller_params.waypoint_radius_m`) і лінією маршруту; колір за статусом — сірий (попереду),
  червоний (поточна ціль), зелений (взята). Прогрес — з `ctx.log("waypoints_reached")` або
  `ctx.log("waypoint_index")`;
- **зони РЕБ** з маніфесту — червоні кола;
- **маркери алгоритму**: `ctx.map_marker("estimate", lat, lon, label="PnP", color="orange")` (створює або
  переміщує маркер за ім'ям; кольори orange / purple / red / green / blue / gray), `ctx.map_remove(name)`;
- рядок стану: час симуляції, взяті точки, висота, швидкість, крен, `ctx.log("status")`; прапорець
  «Стежити за літаком».

Карта — **окремий процес** (multiprocessing): не гальмує цикл керування, закриття вікна не зупиняє тест;
стенд шле лише найсвіжіший стан (`viz.update_hz`, дефолт 5 Гц), іменовані маркери не губляться. Після тесту
вікно лишається відкритим із заголовком «завершено: <причина>»; `uavlab run` завершується, коли його закрити.
Позиція — з `flight_truth` (карта для людини), без нього — з сенсора `drone_geo_position`.

Тайли: `tile.openstreetmap.org` з власним User-Agent стенда — бібліотечний «TkinterMapView» OSM блокує
(замість тайлів картинка «Access blocked»), тому `_patch_tile_requests()` підставляє його в процесі карти
(сама бібліотека не змінюється). Потрібен інтернет. Свій сервер тайлів — `viz.tile_server`.

```yaml
viz:
  enabled: false              # або --map
  follow: true
  update_hz: 5
  waypoints_param: waypoints
  radius_param: waypoint_radius_m
  tile_server: null           # null — OpenStreetMap
```

`uavlab/__main__.py` має guard `if __name__ == "__main__"` — обов'язково: на Windows (spawn) процес карти
заново імпортує головний модуль. `uavlab improve` карту не відкриває (тести без нагляду), якщо `viz.enabled`
не ввімкнено в конфігу явно.

## Синхронізація часу стенда і симулятора

**Проблема.** UE симулює за кадр не більше `min(MaxPhysicsDeltaTime, MaxSubsteps × MaxSubstepDeltaTime)`
секунд фізики (тут 16 × 0.0013 = **0.0208 с**, `FPhysSubstepTask::UpdateTime`). При ~11 FPS кадр триває
~0.09 с, тож фізика проходить лише ~22 % часу світу: `airspeed` 60 м/с, а позиція зміщується на ~13 м/с.
Усі секунди стенда (ліміти, `no_progress_s`, час реакції, критерії) розтягуються в ~4.5 раза — у прогоні
improve `20261108_124908` саме тому жодна версія не дотягла до WP1.

**Рішення — три рівні:**

1. **Фіксований крок часу Unreal** (`simulation.fixed_step_s`, дефолт `0.02`). `START`/`SET_FIXED_STEP`
   → `UTestSessionSubsystem::SetFixedStep()` → `FApp::SetUseFixedTimeStep(true)` + `SetFixedDeltaTime(dt)`:
   кожен кадр — рівно `dt` секунд світу незалежно від FPS. При `dt` ≤ ліміту фізики час світу (`sim_time`)
   = час фізики. Симуляція може йти повільніше (або швидше) за реальний час — це нормально: стенд живе
   в секундах симуляції. Режим глобальний для рушія, тому вимикається в `NotifySimulationStopped()` і
   `Deinitialize()` (меню й вільний політ — знову реальний час). Якщо `dt` > ліміту — попередження в лозі
   Unreal і стенда. Відповідь `START` містить `fixed_step_active`, `fixed_step_s`, `physics_cap_per_frame_s`;
   маніфест — те саме в `physics`.
2. **Стенд живе в часі симуляції.**
   - Крок алгоритму — за `sim_time` шини, а не за годинником ПК: `rate_hz` — кроків на секунду світу;
     `obs.dt` — секунди симуляції від попереднього кроку; `steps.csv` має `dt_sim` поряд із `dt_wall`.
   - **Годинник симуляції в коді алгоритму** (`simulation.sim_clock`, дефолт `true`, `uavlab/simclock.py`):
     у модулях з теки алгоритму та `record.extra_sources` глобальна назва `time` підміняється проксі, у
     якого `monotonic` / `perf_counter` / `time` (+ `*_ns`) повертають час симуляції, решта — справжній
     модуль. Так чужі алгоритми з `time.monotonic()` (фільтри, похідні, оцінка курсу за інтервал, як у
     `gps_autopilote/autopilote.py`) працюють коректно без змін. Не покривається `from time import monotonic`
     — там використовуйте `obs.t` / `obs.dt` / `uavlab.simclock.now()`. Стенд і сторонні бібліотеки не
     зачіпаються. `load_controller` реєструє модуль алгоритму в `sys.modules`.
3. **Контроль.** `flight_truth.time`: `frame_dt_s`, `physics_time_ratio` (EMA проєкції зміщення на швидкість,
   ÷ `DeltaTime`; ~1 — синхронно), `fixed_step`. Метрика `time_sync`: `physics_time_ratio_median/min`
   (для старих збірок — оцінка «шлях за GPS / ∫ шляхової швидкості»), `sim_seconds_per_wall_second`,
   `fixed_step`, `valid` (ratio ≥ `simulation.min_physics_time_ratio`, дефолт 0.9). Невалідний прогін дає
   аномалію `physics_time_lag`, Claude отримує пояснення в системному промпті, а `uavlab improve`
   вважає його **перериванням** (версія лишається `proposed`, `--resume` після виправлення), а не провалом.

```yaml
simulation:
  fixed_step_s: 0.02          # <= physics_cap_per_frame_s (0.0208); 0 / null — реальний час
  sim_clock: true
  min_physics_time_ratio: 0.9
```

## Запис сесії (`sessions/<id>/`)

| Файл | Вміст |
|------|-------|
| `manifest.json` | маніфест з Unreal |
| `config.yaml` | конфіг тесту |
| `controller_src/` | файл алгоритму + сусідні модулі, які він імпортує + `record.extra_sources`; `ENTRY` — ім'я головного файлу |
| `truth.csv` | `flight_truth` на **кожному** пакеті шини (сплощені колонки `geo.altitude_m`, `autopilot.target.roll_deg`…) |
| `steps.csv` | кожен крок алгоритму: `cmd.*`, `step_latency_ms`, `dt_wall`, `cmd_sent`, `cmd_held`, `log.*` (`ctx.log`) |
| `sensors.jsonl` | сирі JSON-payload'и інших сенсорів (≤ `sensors_max_hz`) |
| `events.jsonl` | події Unreal + стенда + `ctx.event` |
| `summary.json` | підсумок: причина зупинки, тривалість, лічильники |
| `metrics.json`, `plots/*.png`, `llm_input.md`, `report.json`, `report.html` | результати аналізу |

## Аналіз

### 1. Детерміновані метрики (`analysis/metrics.py`)

LLM погано рахує по сирих рядах — тому все числове рахується локально:

| Група | Метрики |
|-------|---------|
| `flight` | статистика висоти, швидкості, AoA, ковзання, крену/тангажу, перевантаження, вертикальної швидкості, газу, тяги; пройдена дистанція; частка часу з \|AoA\| > 15° |
| `attitude_tracking` | уставка алгоритму vs факт по крену/тангажу/швидкості рискання: RMS, зсув, max, p95, оцінка запізнення (крос-кореляція), реакція на ступінчасті зміни уставки (перерегулювання, час наростання, встановлення) |
| `oscillation` | домінантна частота 0.1–5 Гц і частка потужності піку (> ~0.3 — виражені автоколивання) для крену, тангажу, висоти, рискання і для кожної команди алгоритму |
| `saturation` | частка часу й найдовший інтервал насичення виходів PID і тяги |
| `controller_commands` | статистика, швидкість зміни, частота змін напрямку (chattering) |
| `controller_timing` | латентність `step()`, фактична частота, джитер, втрачені команди |
| `link` | «вік» команди на боці Unreal, частота шини, найбільший пропуск |
| `environment` | вітер, РЕБ, похибка крену всередині/поза зонами |
| `controller_logs` | статистика кожної змінної `ctx.log` (числові та категоріальні) |
| `anomalies` | часові вікна: велика похибка крену/тангажу, високий AoA, довге насичення, застарілі команди, пропуски телеметрії, зіткнення, винятки, перебування в зоні РЕБ |

### 2. Пакет для LLM (`analysis/pack.py`)

Текстові блоки в тегах: `test` (мета, параметри), `run_summary`, `manifest` (без службових
PID-полів), `metrics`, `events`, `timeseries_overview` (≤ 240 рядків ключових сигналів; сталі
колонки — одним значенням у заголовку), `anomaly_window_N` (≤ 4 вікна до 20 с, ≤ 5 Гц; аномалії, що
перекриваються, зливаються; пріоритет: зіткнення/винятки → AoA → зв'язок → насичення → похибка
трекінгу), `controller_log_overview` (числові `ctx.log` — ≤ 150 рядків; текстові — лише моменти
зміни категорії, числа всередині рядка не рахуються зміною), `controller_source` (≤ 60 тис. символів)
+ PNG-графіки як зображення. Загальний бюджет — `MAX_TOTAL_CHARS` = 100 тис. символів (~30 тис.
токенів): понад нього спершу відкидаються найменш важливі вікна, потім проріджується огляд; метрики,
маніфест і код не обрізаються. Копія того, що надсилається, — `llm_input.md`.

Сенсори в `obs.sensors` декодуються ліниво (`LazySensors`) — лише ті топіки, які алгоритм справді
читає; великі payload'и (custom_objects, lidar) не парсяться на кожному кроці.

### 3. Claude (`analysis/llm_claude.py`)

- Anthropic SDK, модель `claude-opus-5-5` (конфіг `analysis.model`), adaptive thinking,
  `effort` з конфігу, streaming.
- **Structured output** (`output_config.format`, JSON schema `REPORT_SCHEMA`): `summary`,
  `verdict` (`pass|marginal|fail|inconclusive`), `objective_assessment`, `strengths`,
  `issues[]` (`severity`, `category`, `evidence[]`, `root_cause_hypothesis`, **`attribution`**:
  алгоритм користувача / внутрішній PID / умови тесту / симулятор, `recommendation`,
  `code_suggestion`), `recommended_next_tests`, `confidence`, `caveats`.
- Системний промпт описує зв'язку «алгоритм → уставки → внутрішній PID» і вимагає
  посилатися на конкретні метрики/інтервали, розділяти джерела проблем, читати код
  алгоритму (одиниці, wrap-around курсу, насичення, обробка втрати сенсорів).
- Server-side `fallbacks: "default"` (beta `server-side-fallback-2026-07-01`): якщо класифікатор
  безпеки помилково відхилить запит (тематика РЕБ), API сам повторює його на резервній моделі.
- Помилка API не губить сесію: звіт будується з метриками, LLM-частина — `uavlab analyze`.

### 4. Звіт

`report.html` (світла/темна тема): вердикт, мета, сильні сторони, проблеми за severity з
доказами й правками коду, наступні тести, графіки, метрики, таблиця аномалій. Короткий
підсумок друкується в консоль.

## Автоматичне покращення: `uavlab improve`

Замкнений цикл «тест → аналіз → правка Claude → повторний тест» (`uavlab/improve/`):

```powershell
uavlab improve examples/step_response.py -c test_configs/step_response.yaml
uavlab improve my_algo.py -c my_test.yaml --max-iterations 8 --max-cost 3 --allow params,autopilot_pid --approve
uavlab apply improve\<run_id>        # перенести найкращу версію в оригінальні файли
```

1. **v0** — копія алгоритму (файл + сусідні модулі, які він імпортує + `record.extra_sources`) у
   `improve/<run_id>/v0/src/`, тест, аналіз.
2. **Критерії приймання** (`acceptance` у конфігу) — формальні пороги над `metrics.json`:
   `{metric: "attitude_tracking.roll_step_response.t90_s_max", max: 2.0}` / `min:`. Якщо їх немає, Claude
   один раз перетворює `objective` на критерії за списком наявних метрик; вони записуються в
   `acceptance.yaml` і **заморожуються** — агент не може їх змінити (як і мету, критерії зупинки й сам тест).
3. **Ітерація**: нова версія = копія **найкращої** → агент (Claude, tool use) вносить правки → тест в
   окремому процесі (`uavlab run`, чисті імпорти) → аналіз → оцінка. Версія лишається, лише якщо
   `(без провалу, к-сть пройдених критеріїв, −сумарне перевищення)` строго краще (поріг 2 % проти шуму);
   інакше вона відкидається, а агент бачить її в `iteration_history` / `last_attempt_reverted`.
   Провал незалежно від метрик: `crash`, `controller_exception`, `telemetry_lost`, `min_altitude`,
   `max_distance`, переривання.
4. Стоп: усі критерії пройдено · `max_iterations` · `max_cost_usd` · агент вважає, що покращувати нічого.
5. **Валідація** найкращої версії на `improve.validation_configs` (інші сценарії — проти підгонки під один тест).
6. `improve_report.html`: критерії v0 vs найкраща, кожна ітерація (гіпотеза, параметри, PID, diff коду,
   посилання на звіт аналізу), валідація, оцінка вартості. **Оригінали не змінюються** до `uavlab apply`.

**Інструменти агента** (`improve/agent.py`, strict tool schemas; набір — `improve.allow`):

| Інструмент | `allow` | Дія |
|------------|---------|-----|
| `read_file` | завжди | прочитати файл алгоритму версії (з номерами рядків) |
| `edit_code` | `code` | точна заміна `old_str → new_str` (рівно одне входження); відкат, якщо файл не компілюється; заборонені в доданих рядках `subprocess`, `os.system`, `eval`, `exec`, `socket`, `requests`… |
| `set_controller_params` | `params` | `controller_params` версії (`value_json` — JSON-літерал) |
| `set_autopilot_pid` | `autopilot_pid` | `autopilot_pid` версії → `pid_overrides` у `START` (лише на час тестів) |
| `submit_changes` | завжди | завершити ітерацію; перед прийняттям стенд перевіряє, що алгоритм імпортується в окремому процесі |

Контекст агента: мета, заморожені критерії, оцінка найкращої версії, її `controller_params`, ефективні PID з
маніфесту + множник гейн-шедулінгу, звіт аналізу, метрики, історія ітерацій, остання відкинута спроба з
diff, код головного файлу з номерами рядків.

`uavlab apply <run>` — код найкращої версії копіюється в оригінальні файли (резервна копія `*.bak-<час>`),
параметри — у `<конфіг>.improved.yaml` поруч з оригіналом (YAML-коментарі оригіналу не чіпаються), для PID
друкуються значення для Blueprint (`AttitudeControl` → `RollPid`/`PitchPid`/`YawRatePid`) — або залиште
`autopilot_pid` у конфігу.

### Продовження перерваного прогону: `--resume`

```powershell
uavlab improve --resume improve\<run_id>                       # з останнього завершеного кроку
uavlab improve --resume improve\<run_id> --max-iterations 10   # дати більше ітерацій / бюджету
```

Після кожного кроку стан пишеться атомарно в `improve/<run_id>/state.json` (версії з їхніми статусами,
найкраща, історія, заморожені критерії, витрати, фаза); повний конфіг прогону — `run_config.yaml`
(алгоритм і конфіг з `--resume` не вказуються, прапорці CLI перекривають лише секцію `improve`).
Відновлення за статусом версії:

| Де перервано | Статус | Що робить `--resume` |
|--------------|--------|----------------------|
| під час роботи агента (Ctrl+C, помилка API) | `created` | версію прибрано, лічильник ітерацій повернено, агент готує правки заново |
| правки готові, тест не завершився (Ctrl+C, Unreal не відповідає / впав) | `proposed` / `approved` | тест повторюється **без** повторного виклику агента (оплачені правки не губляться); з `--approve` — повторне підтвердження |
| тест завершено, аналіз/оцінка ні | `tested` | лише аналіз + оцінка, без нового тесту (якщо сесія не дописана — тест заново) |
| цикл завершено, валідація/звіт ні | фаза `validation` | доганяє непройдені сценарії валідації |
| прогін завершено | фаза `done` | якщо всі критерії пройдено — лише оновлює звіт; інакше продовжує цикл (з більшим `--max-iterations` / `--max-cost`) |

Тест без сесії (Unreal не відповідає) — це **переривання**, а не провал версії: версія лишається
непротестованою. Винятки самого алгоритму дають сесію зі `stop_reason=controller_exception` і рахуються
як провал версії. При перериванні друкується проміжний звіт і команда для продовження (код виходу 130 —
Ctrl+C, 2 — інше).

Конфіг (`improve:`): `max_iterations` (5), `max_cost_usd` (5.0, оцінка за токенами), `allow`
(`[params, code, autopilot_pid]`), `approve` (false — `--approve` показує diff і питає перед кожним тестом),
`model`, `effort`, `max_agent_turns` (20), `analyze_each_iteration` (true; false — дешевше, агент бачить лише
метрики), `validation_configs`, `pause_between_runs_s`, `runs_dir` (`improve`).

Вартість: ітерація ≈ 1 тест + аналіз ($0.1–0.3) + агент ($0.05–0.3). Перевірено на мок-симуляторі:
з `Kp = 1`, кривою-множником 0.1 і `Ki = 0` агент за одну ітерацію вимкнув криву, підняв Kp і додав Ki
тангажу — 1/4 → 4/4 критеріїв, ≈ $0.14 (без LLM-аналізу ітерацій).

## Перевірка без Unreal

Мок-симулятор (4 ZMQ-канали) легко написати на ~100 рядках Python; стенд проганявся
на такому моку наскрізно: `start: auto` і `start: wait`, зупинка зі стенда (`time_limit`) і
з «Unreal» (`session_end`), приклад `waypoints_gps` з `use_truth: false` і `extra_sources`,
`uavlab analyze --no-llm`.

## Типові проблеми

| Симптом | Причина / рішення |
|---------|-------------------|
| `Unreal не відповідає на канал керування сесією` | PIE/гра не запущена, або порт 5557 зайнятий іншим екземпляром PIE (лог `TestSessionSubsystem: ZMQ bind failed`). Або `start: wait` — режим сумісності без маніфесту |
| `автопілот не активний` у відповіді START | режим без автопілота (`Free`, `PlaybackAndTrack`…) — використовуйте `AutoTrack` / `PlaybackAndAutoTrack` |
| `telemetry_lost` одразу після старту | на літаку немає `USensorBusComponent` (додається в Blueprint) або `SensorsMode = Target` у режимі без цілі |
| порожній `truth.csv` | `bEnableSensorFlightTruth = false` або літак без тегу `AutoTracker` при `SensorsMode = None` |
| LLM-аналіз не вдався | немає `ANTHROPIC_API_KEY` / пакета `anthropic`; повторіть `uavlab analyze sessions/<id>` |
