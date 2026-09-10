# 10 — Конвеєр аеродинамічних даних

Мета: із геометрії профілю крила отримати `DataTable` рядків `FAerodynamicProfileRow`
(криві CL/CD/Cm від кута атаки, ключовані за кутом закрилка), які читає
`USubAerodynamicSurfaceSC` під час польоту.

## Два шляхи

### XFoil (швидкий, 2D панельний метод)

1. Спроєктувати геометрію в **OpenVSP** (`Tools/OpenVSP/`).
2. Згенерувати поляри **XFoil** (`Tools/XFoil/`) через
   `AerodynamicPhysicalCalculationUtil::CalculatePolar`.
3. Екстраполювати до 360° AoA — **`Tools/Airfoil/airfoil.py`** (метод Viterna через
   пакет `airfoilprep`).

`CalculatePolar(PathToProfile, RootChord, TipChord, Span, DeflStart, DeflEnd,
Sweep, HingeLocation, SurfaceName, SubSurfaceIndex)` — аналог `xfoil.exe < com.txt`:
видаляє/копіює `Tools/XFoil/polar.dat`, генерує `commands_generated.txt` із
підставленими шляхами, запускає `cmd.exe /C "xfoil.exe < commands_generated.txt"`
з робочою текою `Tools/XFoil/`. Результат XFoil пише в `Tools/XFoil/polar.txt`.

### SU2 (вища точність CFD, основний для продакшн-полярів)

1. `AerodynamicPhysicalCalculationUtil::RunSU2Calculation(ProfilePath, SurfaceName,
   HingeLocation, MinFlap, MaxFlap)` з Unreal → викликає
   `Tools/SU2/execute_su2_calculation.py` через `PythonScriptPlugin`.
   Хардкод-конфіг: `CoreNumber=4`, `HingeLocation=0.75`, `FlapStep=1.0`,
   `RmsQuality=-4`, `Resume=true`.
2. `Tools/SU2/sweep.py` — повний sweep полярів по кутах закрилка на SU2-v8.4.0;
   результати кешуються в `Tools/SU2/logs/`.
3. `Tools/SU2/su2_unreal_import.py` — автоматично створює `DataTable`-ресурси під
   `/Game/`.
4. **Іменування ресурсу:** крапки у float стають дефісами — напр.
   `/Game/WingProfile/NACA_0009/DT_naca0009_0-75_-40-0_40-0`.
   `BuildAssetPath` / `FormatFloatForAsset` відтворюють це правило з
   `su2_unreal_import.py`.

Standalone SU2 (без Unreal): `Tools/SU2/run_unrealless.bat` (обгортка
`unrealless_run.py` / `unrealless_run_list.py`); візуалізація —
`unrealless_visualise.py`, `visualise_geometry.py`.

## Спільний фінальний крок — плагін `AirfoilImporter`

`Plugins/AirfoilImporter/` — власний **редакторний** плагін.

- **`UAirfoilDATFactory`** (`UFactory`) — імпортує `.dat`-файли профілю крила
  (`FactoryCreateText` / `FactoryCanImport`) у `DataTable` рядків
  **`FAirfoilPointData`** (`X`, `Z` — точки контуру профілю, `FTableRowBase`).
- Ця `DataTable` точок вішається на `UAerodynamicSurfaceSC::Profile`.
- Поляри (CL/CD/Cm) імпортуються окремою `DataTable`
  `FAerodynamicProfileRow` (SU2-пайплайном або вручну) і вішаються на
  `FAerodynamicSurfaceStructure::AerodynamicTable`.

## Запуск із Unreal

`UFlightDynamicsComponent::GenerateAerodynamicPhysicalConfigutation()`
(`CallInEditor`, «Розрахувати поляри для ЛА») →
`AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(Surfaces)`:
для кожної пари сусідніх станцій `SurfaceForm` кожної поверхні запускає розрахунок
полярів, зберігає `DataTable`, прив'язує (`AttachAssetToSurface`).

## Утилітні класи конвеєра

| Клас | Роль |
|------|------|
| `AerodynamicPhysicalCalculationUtil` | Оркестрація: `GenerateAerodynamicPhysicalConfigutation`, `CalculatePolar` (XFoil), `RunSU2Calculation` (SU2), `FindPathToProfile` (єдиний `.dat` у теці пакету `DataTable`), `BuildAssetPath` / `BuildAssetPathForVSpaero` / `FormatFloatForAsset`, `DoesAssetExist`, `AttachAssetToSurface` |
| `AerodynamicToolRunner` | Низькорівневий I/O + запуск процесів (Python/XFoil/OpenVSP), парсинг полярних файлів. **Обмеження: ≤ 45 символів на шлях тимчасової теки** — довші ламають XFoil/SU2 на Windows (тихо) |
| `AerodynamicUtil` | Геометрія профілю (chord-finding, масштабування, нормалізація, 2D→3D) |
| `AerodynamicProfileLookup` | Пошук рядка полярів за `FLAP_{angle}_Deg` |

## Залежності інструментів

- `Tools/Airfoil/airfoil.py` потребує Python-пакета `airfoilprep`;
  `Tools/PyInstall/` (містить `airfoilprep/`) і `Tools/OpenVSP/install_to_py.bat`
  налаштовують залежності.
- Python для скриптів — бандл-Python Unreal Engine:
  `C:\Program Files\Epic Games\UE_{version}\Engine\Binaries\ThirdParty\Python3\Win64`.
- Артефакти інструментів у репо: `Tools/OpenVSP/OpenVSP-3.45.4-win64.7z`,
  `Tools/SU2/SU2-v8.4.0-win64.zip`, `Tools/XFoil/` (`xfoil.py`, `naca0009.dat`,
  `polar.*`, `commands*.txt`).
- **Не комітити**: `Saved/Cesium/...` (SQLite-кеш геотайлів, ~305 МБ).

## Плагін `Unreal5ZeroMQ`

`Plugins/Unreal5ZeroMQ/` — обгортка libzmq 4.3.x (`ThirdParty/libzmq_4.3.1/`,
Windows/Linux/MacOS бінарники). Модуль `ZeroMQ` (`FZeroMQModule`) лінкується
`PublicDependencyModuleNames` головного модуля. Використовується
`USensorBusComponent` (PUB) та `UAttitudeControlComponent` (PULL) через
`#include <zmq.hpp>` у `.cpp` (заголовок ніколи не «протікає» в `.h`; після
`zmq.hpp` іде `#undef OPAQUE` — конфлікт `<wingdi.h>` із `CesiumGltf::Material`).
