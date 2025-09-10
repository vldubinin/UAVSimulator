#pragma once

#include "CoreMinimal.h"
#include "AssetToolsModule.h"
#include "Factories/DataAssetFactory.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UAVSimulator/Util/TextUtil.h"
#include "HAL/PlatformProcess.h"
#include "UAVSimulator/Entity/PolarRow.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"

/**
 *
 */
class AerodynamicPhysicalCalculationUtil
{

public:
	static void GenerateAerodynamicPhysicalConfigutation(UObject* ContextObject, TArray<UAerodynamicSurfaceSC*> Surfaces);
	static TMap<float, PolarRow> CalculatePolar(FString PathToProfile, float FlapPosition, int FlapAngle);
	static FString FindPathToProfile(UAerodynamicSurfaceSC* Surface);
	static FString GetAiroplaneFolderName(UObject* ContextObject);
	static FString GetOrCreateTempWorkDirectory();
	static FString ToAbsolutePath(FString Path);
	static FString CopyToTempWorkDirectory(FString SourcePath, FString Name);
	static TMap<float, PolarRow> ReadPolarFile(FString Path, int AoAIndex, int ClIndex, int CdIndex, int CmIndex);
	static bool CleanTempWorkDirectory();
	static FString SavePolarForAirfoil(TMap<float, PolarRow> Polar);
	static bool ExecutePythonScript(FString Command);
};

