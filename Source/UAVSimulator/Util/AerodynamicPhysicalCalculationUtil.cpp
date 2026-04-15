#include "AerodynamicPhysicalCalculationUtil.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/AerodynamicToolRunner.h"
#include "UAVSimulator/Util/TextUtil.h"
#include "UAVSimulator/Structure/AerodynamicSurfaceStructure.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Engine/DataTable.h"

void AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(TArray<UAerodynamicSurfaceSC*> Surfaces)
{
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		FString PathToProfileFile = FindPathToProfile(Surface);
		Surface->Profile->GetPackage();
		TArray<FAerodynamicSurfaceStructure>& SubSurfaces = Surface->SurfaceForm;

		for (int32 i = 0; i < SubSurfaces.Num() - 1 && SubSurfaces.Num() > 1; i++)
		{
			FAerodynamicSurfaceStructure& RootSurface = SubSurfaces[i];
			float HingeLocation = ((RootSurface.StartFlapPosition + RootSurface.EndFlapPosition) / 2) * 0.01;
			FString AssetPath = BuildAssetPath(PathToProfileFile, HingeLocation, RootSurface.MinFlapAngle, RootSurface.MaxFlapAngle);

			if (!DoesAssetExist(AssetPath)) {
				RunSU2Calculation(PathToProfileFile,
					*Surface->GetName(),
					HingeLocation, 
					RootSurface.MinFlapAngle,
					RootSurface.MaxFlapAngle);
			}
			if (DoesAssetExist(AssetPath)) {
				Surface->Modify();
				AttachAssetToSurface(RootSurface, AssetPath);
			}
			else {
				UE_LOG(LogUAV, Error, TEXT("GenerateAerodynamicPhysicalConfigutation: не вдалося створити ассет: '%s'."), *AssetPath);
			}
		}
	}
}

/*void AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(TArray<UAerodynamicSurfaceSC*> Surfaces)
{
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		FString PathToProfileFile = FindPathToProfile(Surface);
		Surface->Profile->GetPackage();
		TArray<FAerodynamicSurfaceStructure> SubSurfaces = Surface->SurfaceForm;

		// Кожна пара сусідніх секцій SurfaceForm утворює одну трапецієвидну підсекцію крила
		for (int32 i = 0; i < SubSurfaces.Num() - 1 && SubSurfaces.Num() > 1; i++)
		{
			const FAerodynamicSurfaceStructure& RootSurface = SubSurfaces[i];
			const FAerodynamicSurfaceStructure& TipSurface = SubSurfaces[i + 1];

			// Розмах секції задається зміщенням по Y між сусідніми секціями
			CalculatePolar(PathToProfileFile,
				(int32)RootSurface.ChordSize,
				(int32)TipSurface.ChordSize,
				(int32)TipSurface.Offset.Y,
				RootSurface.MinFlapAngle,
				RootSurface.MaxFlapAngle,
				*Surface->GetName(),
				i);
		}
		break;
	}
}*/

/*TMap<float, FPolarRow> AerodynamicPhysicalCalculationUtil::CalculatePolar(
	FString PathToProfile,
	int32 RootChord, int32 TipChord, int32 Span,
	int32 DeflectionAngleStart, int32 DeflectionAngleEnd,
	int32 Sweep, FString SurfaceName, int32 SubSurfaceIndex)
{
	const FString ScriptPath = FPaths::ProjectDir() + TEXT("Tools/XFoil/xfoil.py");

	// Формуємо рядок команди: шлях до скрипту + усі параметри секції як аргументи
	const FString Command = FString::Printf(
		TEXT("\"%s\" \"%d\" \"%d\" \"%d\" \"%d\" \"%d\" \"%d\" \"%s\" \"%d\" \"%s\""),
		*ScriptPath,
		RootChord, TipChord, Span,
		DeflectionAngleStart, DeflectionAngleEnd,
		Sweep,
		*SurfaceName,
		SubSurfaceIndex,
		*PathToProfile);

	AerodynamicToolRunner::RunPythonScript(Command);

	// Результати зберігаються Python-скриптом на диску; тут повертаємо порожню мапу
	return TMap<float, FPolarRow>();
}*/

TMap<float, FPolarRow> AerodynamicPhysicalCalculationUtil::CalculatePolar(
	FString PathToProfile,
	int32 RootChord, int32 TipChord, int32 Span,
	int32 DeflectionAngleStart, int32 DeflectionAngleEnd,
	int32 Sweep, FString SurfaceName, int32 SubSurfaceIndex)
{
	IFileManager& FM = IFileManager::Get();
	const FString XFoilDir = FPaths::ProjectDir() + TEXT("Tools/XFoil/");

	const FString PolarDatPath   = XFoilDir + TEXT("polar.dat");
	const FString XfoilExePath   = XFoilDir + TEXT("xfoil.exe");
	const FString CommandsPath   = XFoilDir + TEXT("commands_generated.txt");

	// Видаляємо попередній polar.dat щоб уникнути використання застарілих даних
	if (FM.FileExists(*PolarDatPath))
	{
		FM.Delete(*PolarDatPath);
	}

	// Копіюємо профіль крила як polar.dat у робочу директорію XFoil
	if (FM.Copy(*PolarDatPath, *PathToProfile) != COPY_OK)
	{
		UE_LOG(LogUAV, Error, TEXT("CalculatePolar: не вдалося скопіювати '%s' → '%s'"), *PathToProfile, *PolarDatPath);
		return TMap<float, FPolarRow>();
	}

	// Конвертуємо шляхи у абсолютні (потрібно для зовнішнього процесу)
	const FString AbsXfoilExe  = FM.ConvertToAbsolutePathForExternalAppForRead(*XfoilExePath);
	const FString AbsCommands  = FM.ConvertToAbsolutePathForExternalAppForRead(*CommandsPath);

	// Генеруємо файл команд XFoil — це те саме що com.txt але з підставленими шляхами
	const FString XFoilCommands =
		TEXT("LOAD polar.dat\r\n")  // назва файлу відносно робочої директорії XFoil
		TEXT("GDES\r\n")
		TEXT("FLAP\r\n")
		TEXT("0.7\r\n")             // flap_position — відсоток хорди
		TEXT("999\r\n")             // span-wise station (999 = tip)
		TEXT("0.5\r\n")             // hinge z-position
		TEXT("25\r\n")             // flap_angle
		TEXT("EXEC\r\n")
		TEXT("\r\n")
		TEXT("PANE\r\n")
		TEXT("PPAR\r\n")
		TEXT("N\r\n")
		TEXT("200\r\n")
		TEXT("\r\n")
		TEXT("\r\n")
		TEXT("OPER\r\n")
		TEXT("v\r\n")
		TEXT("300000\r\n")
		TEXT("ITER\r\n")
		TEXT("40\r\n")
		TEXT("PACC\r\n")
		TEXT("polar.txt\r\n")       // вихідний файл полярних характеристик
		TEXT("\r\n")
		TEXT("ASEQ\r\n")   // позитивний діапазон AoA: від 0 до +20 з кроком 0.5°
		TEXT("0\r\n")
		TEXT("20\r\n")
		TEXT("0.5\r\n")
		TEXT("INIT\r\n")   // скидаємо стан граничного шару після розбіжності першого діапазону
		TEXT("ASEQ\r\n")   // негативний діапазон AoA: від 0 до -13 з кроком 0.5°
		TEXT("0\r\n")
		TEXT("-13\r\n")
		TEXT("0.5\r\n")
		TEXT("\r\n")
		TEXT("QUIT\r\n");

	FFileHelper::SaveStringToFile(XFoilCommands, *CommandsPath);

	// Запускаємо xfoil.exe через cmd.exe з перенаправленням stdin — точно як "xfoil.exe < com.txt"
	// Подвійні лапки навколо всієї команди після /C обов'язкові при шляхах з пробілами
	const FString CmdArgs = FString::Printf(TEXT("/C \"\"%s\" < \"%s\"\""), *AbsXfoilExe, *AbsCommands);
	FProcHandle Handle = FPlatformProcess::CreateProc(
		TEXT("cmd.exe"), *CmdArgs,
		false,   // bLaunchDetached
		true,    // bLaunchHidden
		true,    // bLaunchReallyHidden
		nullptr, 0,
		*XFoilDir,  // робоча директорія — та ж що й при запуску з командного рядка
		nullptr);

	if (Handle.IsValid())
	{
		FPlatformProcess::WaitForProc(Handle);
		FPlatformProcess::CloseProc(Handle);
		UE_LOG(LogUAV, Log, TEXT("CalculatePolar: XFoil завершив роботу, результат у 'polar.txt'"));
	}
	else
	{
		UE_LOG(LogUAV, Error, TEXT("CalculatePolar: не вдалося запустити cmd.exe для xfoil.exe"));
	}

	return TMap<float, FPolarRow>();
}

void AerodynamicPhysicalCalculationUtil::RunSU2Calculation(
	FString ProfilePath, FString SurfaceName, float HingeLocation, float MinFlap, float MaxFlap)
{
	// Hardcoded SU2 sweep configuration
	const int32  CoreNumber    = 4;
	const float  FlapStep      = 1.0f;
	const int32  RmsQuality    = -6;
	const TCHAR* Resume        = TEXT("true");

	const FString ScriptPath = FPaths::ProjectDir() + TEXT("Tools/SU2/execute_su2_calculation.py");

	// argv order expected by execute_su2_calculation.py:
	//   <profile_path> <surface_name> <cores> <hinge> <min_flap> <max_flap> <flap_step> <rms_quality> <resume>
	const FString Command = FString::Printf(
		TEXT("\"%s\" \"%s\" \"%s\" \"%d\" \"%g\" \"%g\" \"%g\" \"%g\" \"%d\" \"%s\""),
		*ScriptPath,
		*ProfilePath,
		*SurfaceName,
		CoreNumber,
		HingeLocation,
		MinFlap,
		MaxFlap,
		FlapStep,
		RmsQuality,
		Resume);

	UE_LOG(LogUAV, Log, TEXT("RunSU2Calculation: %s"), *Command);
	AerodynamicToolRunner::RunPythonScript(Command);
}

// ---------------------------------------------------------------------------
// Private helper — matches Python's str(float).replace(".", "-")
// FString::SanitizeFloat strips trailing zeros, so 0.0f -> "0" (no dot).
// Python always emits a dot for floats: str(0.0) == "0.0". We replicate that.
// ---------------------------------------------------------------------------
static FString FmtFloat(float V)
{
	FString S = FString::SanitizeFloat(V);
	if (!S.Contains(TEXT(".")))
		S += TEXT(".0");
	S.ReplaceInline(TEXT("."), TEXT("-"));
	return S;
}

FString AerodynamicPhysicalCalculationUtil::BuildAssetPath(
	const FString& ProfilePath, float HingeLocation, float MinFlap, float MaxFlap)
{
	FString NormPath = ProfilePath;
	FPaths::NormalizeFilename(NormPath);

	// Make relative to the Content directory to extract the asset folder
	FString RelPath = NormPath;
	FString ContentDir = FPaths::ProjectContentDir();
	FPaths::NormalizeFilename(ContentDir);
	FPaths::MakePathRelativeTo(RelPath, *ContentDir);

	// e.g. "WingProfile/NACA_0009"
	const FString Folder = FPaths::GetPath(RelPath);
	// e.g. "naca0009"
	const FString Stem = FPaths::GetBaseFilename(NormPath);

	const FString Name = FString::Printf(TEXT("DT_%s_%s_%s_%s"),
		*Stem,
		*FmtFloat(HingeLocation),
		*FmtFloat(MinFlap),
		*FmtFloat(MaxFlap));

	return FString::Printf(TEXT("/Game/%s/%s"), *Folder, *Name);
}

bool AerodynamicPhysicalCalculationUtil::DoesAssetExist(const FString& AssetPath)
{
	return FPackageName::DoesPackageExist(AssetPath);
}

bool AerodynamicPhysicalCalculationUtil::AttachAssetToSurface(
	FAerodynamicSurfaceStructure& Structure, const FString& AssetPath)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, *AssetPath);
	if (!Table)
	{
		UE_LOG(LogUAV, Error, TEXT("AttachAssetToSurface: failed to load '%s'"), *AssetPath);
		return false;
	}
	Structure.AerodynamicTable = Table;
	return true;
}

FString AerodynamicPhysicalCalculationUtil::FindPathToProfile(UAerodynamicSurfaceSC* Surface)
{
	// Перетворюємо Unreal-шлях "/Game/..." на реальний шлях у файловій системі
	FString ProfilePath = Surface->Profile->GetPathName();
	ProfilePath.RemoveFromStart("/Game/");
	ProfilePath = TextUtil::RemoveAfterSymbol(ProfilePath, '/');  // Залишаємо лише директорію
	ProfilePath = FPaths::ProjectContentDir() + ProfilePath;

	// Шукаємо .dat файл у директорії; очікуємо рівно один файл
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *ProfilePath, TEXT("*.dat"));

	if (FoundFiles.Num() != 1)
	{
		UE_LOG(LogUAV, Error, TEXT("FindPathToProfile: expected exactly 1 .dat file in '%s', found %d"), *ProfilePath, FoundFiles.Num());
		return FString();
	}

	FString RelativePath = FPaths::Combine(ProfilePath, FoundFiles[0]);
	return IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RelativePath);
}
