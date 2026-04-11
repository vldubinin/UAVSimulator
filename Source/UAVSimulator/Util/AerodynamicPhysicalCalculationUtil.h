#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileAndFlapRow.h"
#include "UAVSimulator/Entity/PolarRow.h"
#include "UAVSimulator/Structure/AerodynamicSurfaceStructure.h"

/**
 * Orchestrates aerodynamic polar generation for aircraft surfaces.
 * File I/O, process execution, and polar parsing are delegated to AerodynamicToolRunner.
 */
class UAVSIMULATOR_API AerodynamicPhysicalCalculationUtil
{
public:
	/**
	 * Генерує аеродинамічні полярні характеристики для всіх підсекцій кожної поверхні зі списку
	 * та зберігає результати як DataTable-ресурси в проекті.
	 * Для кожної пари сусідніх секцій SurfaceForm запускає CalculatePolar.
	 * @param Surfaces — масив аеродинамічних поверхонь ЛА для обробки.
	 */
	static void GenerateAerodynamicPhysicalConfigutation(TArray<UAerodynamicSurfaceSC*> Surfaces);

	/**
	 * Запускає XFoil для однієї секції поверхні — аналог виклику "xfoil.exe < com.txt" з командного рядка:
	 *   1. Видаляє Tools/XFoil/polar.dat якщо існує.
	 *   2. Копіює PathToProfile як Tools/XFoil/polar.dat.
	 *   3. Генерує файл команд Tools/XFoil/commands_generated.txt з підставленими шляхами.
	 *   4. Запускає cmd.exe /C "xfoil.exe < commands_generated.txt" з робочою директорією Tools/XFoil/.
	 * Результат записується XFoil у Tools/XFoil/polar.txt.
	 * @param PathToProfile       — абсолютний шлях до .dat файлу профілю крила.
	 * @param RootChord           — довжина кореневої хорди в мм.
	 * @param TipChord            — довжина кінцевої хорди в мм.
	 * @param Span                — розмах секції в мм.
	 * @param DeflectionAngleStart — початковий кут відхилення закрилка.
	 * @param DeflectionAngleEnd  — кінцевий кут відхилення закрилка.
	 * @param Sweep               — кут стрілоподібності (наразі 0).
	 * @param SurfaceName         — ім'я поверхні для іменування вихідних файлів.
	 * @param SubSurfaceIndex     — індекс підсекції в масиві SurfaceForm.
	 * @return Порожня мапа (результати записуються XFoil безпосередньо у polar.txt).
	 */
	static TMap<float, FPolarRow> CalculatePolar(FString PathToProfile,
		int32 RootChord, int32 TipChord, int32 Span,
		int32 DeflectionAngleStart, int32 DeflectionAngleEnd,
		int32 Sweep, FString SurfaceName, int32 SubSurfaceIndex);

	/**
	 * Запускає Tools/SU2/execute_su2_calculation.py для заданого профілю.
	 * Передає параметри як аргументи командного рядка через UE PythonScriptPlugin.
	 * Hardcoded конфігурація: CoreNumber=4, HingeLocation=0.75, FlapStep=1.0, RmsQuality=-4, Resume=true.
	 * @param ProfilePath — абсолютний шлях до .dat файлу профілю крила.
	 * @param SurfaceName — ім'я поверхні (використовується лише для логування у Python).
	 * @param MinFlap     — мінімальний кут відхилення закрилка.
	 * @param MaxFlap     — максимальний кут відхилення закрилка.
	 */
	static void RunSU2Calculation(FString ProfilePath, FString SurfaceName, float HingeLocation, float MinFlap, float MaxFlap);

	/**
	 * Знаходить .dat файл профілю, пов'язаний з DataTable-ресурсом поверхні.
	 * Шукає єдиний .dat файл у директорії пакету DataTable.
	 * @param Surface — аеродинамічна поверхня з налаштованим Profile.
	 * @return Абсолютний шлях до .dat файлу; порожній рядок якщо файл не знайдено або знайдено більше одного.
	 */
	static FString FindPathToProfile(UAerodynamicSurfaceSC* Surface);

	/**
	 * Будує Unreal-шлях до DataTable-ресурсу, створеного SU2-пайплайном.
	 * Відтворює правило іменування з su2_unreal_import.py (_asset_name / _asset_folder_from_profile).
	 * Крапки у float-значеннях замінюються на дефіси (Unreal не допускає крапки в іменах ресурсів).
	 * @param ProfilePath  — абсолютний шлях до .dat файлу профілю (містить сегмент "Content").
	 * @param HingeLocation — положення шарніру закрилка (0..1).
	 * @param MinFlap       — мінімальний кут відхилення закрилка.
	 * @param MaxFlap       — максимальний кут відхилення закрилка.
	 * @return Unreal object path, наприклад /Game/WingProfile/NACA_0009/DT_naca0009_0-75_-40-0_40-0
	 */
	static FString BuildAssetPath(const FString& ProfilePath, float HingeLocation, float MinFlap, float MaxFlap);

	/**
	 * Перевіряє, чи існує DataTable-ресурс за вказаним Unreal-шляхом.
	 * @param AssetPath — Unreal object path (/Game/...), наприклад результат BuildAssetPath.
	 * @return true якщо пакет існує на диску.
	 */
	static bool DoesAssetExist(const FString& AssetPath);

	/**
	 * Завантажує DataTable за Unreal-шляхом і прив'язує його до поля AerodynamicTable структури.
	 * @param Structure — посилання на FAerodynamicSurfaceStructure для заповнення.
	 * @param AssetPath — Unreal object path (/Game/...), наприклад результат BuildAssetPath.
	 * @return true якщо ресурс успішно завантажено та прив'язано.
	 */
	static bool AttachAssetToSurface(FAerodynamicSurfaceStructure& Structure, const FString& AssetPath);
};
