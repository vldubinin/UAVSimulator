#include "AerodynamicPhysicalCalculationUtil.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/AerodynamicToolRunner.h"
#include "UAVSimulator/Util/TextUtil.h"
#include "UAVSimulator/Structure/AerodynamicSurfaceStructure.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

void AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(TArray<UAerodynamicSurfaceSC*> Surfaces)
{
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		FString PathToProfileFile = FindPathToProfile(Surface);
		Surface->Profile->GetPackage();
		TArray<FAerodynamicSurfaceStructure> SubSurfaces = Surface->SurfaceForm;

		for (int32 i = 0; i < SubSurfaces.Num() - 1 && SubSurfaces.Num() > 1; i++)
		{
			const FAerodynamicSurfaceStructure& RootSurface = SubSurfaces[i];
			const FAerodynamicSurfaceStructure& TipSurface  = SubSurfaces[i + 1];

			CalculatePolar(PathToProfileFile,
				(int32)RootSurface.ChordSize,
				(int32)TipSurface.ChordSize,
				(int32)TipSurface.Offset.Y,
				RootSurface.MinFlapAngle,
				RootSurface.MaxFlapAngle,
				0 /* Sweep */,
				*Surface->GetName(),
				i);
		}
	}
}

TMap<float, FPolarRow> AerodynamicPhysicalCalculationUtil::CalculatePolar(
	FString PathToProfile,
	int32 RootChord, int32 TipChord, int32 Span,
	int32 DeflectionAngleStart, int32 DeflectionAngleEnd,
	int32 Sweep, FString SurfaceName, int32 SubSurfaceIndex)
{
	const FString ScriptPath = FPaths::ProjectDir() + TEXT("Tools/OpenVSP/openvsp_vspaero.py");

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

	return TMap<float, FPolarRow>();
}

FString AerodynamicPhysicalCalculationUtil::FindPathToProfile(UAerodynamicSurfaceSC* Surface)
{
	FString ProfilePath = Surface->Profile->GetPathName();
	ProfilePath.RemoveFromStart("/Game/");
	ProfilePath = TextUtil::RemoveAfterSymbol(ProfilePath, '/');
	ProfilePath = FPaths::ProjectContentDir() + ProfilePath;

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
