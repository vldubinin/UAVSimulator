# Документація UAVSimulator

Симулятор льотної динаміки БпЛА на Unreal Engine 5.7. C++ ядро аеродинаміки,
конвеєр комп'ютерного зору (OpenCV), геопросторова прив'язка (Cesium), сенсорна
шина телеметрії на ZeroMQ, генератори навчальних даних для ML та інтеграція із
зовнішніми інструментами аеродинамічного аналізу (XFoil, SU2, OpenVSP).

> Первинний, авторитетний опис репозиторію для агентів — `CLAUDE.md` у корені
> проєкту. Ця папка розширює його: детально описує кожен модуль, кожен компонент,
> формати повідомлень і взаємодію підсистем. Якщо `CLAUDE.md` і `Docs/`
> розходяться — правий `Docs/` (оновлюється разом із кодом), про розбіжність слід
> повідомити.

## Зміст

| Документ | Що описує |
|----------|-----------|
| [01-Architecture.md](01-Architecture.md) | Загальна архітектура: `AAirplane` та його компоненти, порядок тіку, `UUAVSimulationSubsystem`, `AUAVSimulatorGameModeBase`, режими симуляції, ролі літаків, модулі й залежності збірки |
| [02-FlightDynamics.md](02-FlightDynamics.md) | Аеродинамічний рушій: ієрархія поверхонь, розрахунок сил, вихоровий слід і індуктивний опір, динаміка приводів керма, модель двигуна, кеш фізичного стану |
| [03-PilotInput-and-Autopilot.md](03-PilotInput-and-Autopilot.md) | Конвеєр керування пілота (`IPilotInputSource`), клавіатура/геймпад, координатор `UPilotInputComponent`, автопілот `UAttitudeControlComponent`, PID-регулятор, ZMQ-протокол уставок атитюду |
| [04-SensorBus.md](04-SensorBus.md) | Сенсорна шина ZeroMQ: `IUAVSensorInterface`, `USensorBusComponent`, повний перелік сенсорів і формати їх JSON/бінарних навантажень |
| [05-SurroundingsScanners.md](05-SurroundingsScanners.md) | Сканери оточення: `UCesiumSurroundingsScannerComponent` (метадані 3D Tiles) та `UCustomSurroundingsScannerComponent` (об'єкти з JSON), прив'язка до рельєфу, персистентне сховище об'єктів |
| [06-Camera-and-Vision.md](06-Camera-and-Vision.md) | `UUAVCameraComponent`: RGB-захоплення, OpenCV-обробка, маска сегментації, фонові потоки JPEG-кодування, реєстрація камери в Cesium |
| [07-Recording-Playback-Modes.md](07-Recording-Playback-Modes.md) | Запис і відтворення траєкторій: `UFlightRecorderComponent`, `UFlightPlaybackComponent`, `UFlightScenarioSave`, повний розбір усіх `ESimulatorMode` |
| [08-UI-and-Settings.md](08-UI-and-Settings.md) | Меню симулятора: `USimulatorMenuWidget`, секції налаштувань, персистентність через `USaveGame`, `AUAVSimulatorPlayerController`, HUD-віджети |
| [09-Dataset-Generation.md](09-Dataset-Generation.md) | Редакторні інструменти генерації синтетичних даних: силует дрона, ключові точки, об'єкти сцени, YOLO-датасет маркерів мапи |
| [10-Aero-Data-Pipeline.md](10-Aero-Data-Pipeline.md) | Конвеєр аеродинамічних полярів: OpenVSP → XFoil / SU2 → екстраполяція 360° → плагін `AirfoilImporter` → `DataTable` |
| [11-External-Tooling.md](11-External-Tooling.md) | Зовнішні Python-інструменти: ZMQ-сімейство `Tools/TestingPlatform/` (наведення на слабкому залізі, автопілот «коло», розмітка мапи, запис датасетів позиціонування) і файлове `Tools/ProjectTools/` (інструмент карти) |
| [12-Niagara_VLM_Setup.md](12-Niagara_VLM_Setup.md) | Покрокове налаштування Niagara-системи візуалізації вихорового сліду (VLM / Біо-Савар на GPU) |
| [13-Environment-Actors.md](13-Environment-Actors.md) | Об'єкти середовища: `AEnvironmentActorManager`, зони РЕБ (`AEWZoneActor`), вітрові вектори (`AWindActor`, реально впливають на аеродинаміку), інструмент карти `configurate_env_actors.py`, формат `env_actors.json` |

## Швидкий старт для читача коду

1. **Точка входу** — `AUAVSimulatorGameModeBase::BeginPlay` штовхає прапорці
   в `UUAVSimulationSubsystem`; фактичний спавн літаків робить
   `StartSimulation()`, який викликається з UI (кнопка «Старт» у меню).
2. **Літак** — `AAirplane` (`Actor/Airplane.h`). Єдиний пілотований pawn.
   `PhysicalAirplane` видалено — не згадувати.
3. **Фізика** — уся аеродинаміка в `UFlightDynamicsComponent`
   (`Components/FlightDynamicsComponent.cpp`).
4. **Керування** — усе йде через `UPilotInputComponent` (єдиний, хто пише
   контрольний API динаміки польоту).
5. **Телеметрія назовні** — `USensorBusComponent` публікує ZMQ PUB на
   `tcp://*:5555`; автопілот приймає ZMQ PULL на `tcp://*:5556`.
