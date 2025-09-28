#include "AerodynamicPhysicalCalculationUtil.h"
#include "Misc/Paths.h"
#include "IPythonScriptPlugin.h"
#include "HAL/PlatformFileManager.h"
#include "PythonScriptTypes.h"
#include "Misc/FileHelper.h"
#include "AssetToolsModule.h"
#include "Factories/DataTableFactory.h"
#include "Engine/DataTable.h"
#include "UAVSimulator/Structure/AerodynamicSurfaceStructure.h"
#include <Runtime/Core/Public/Internationalization/Regex.h>
#include <Runtime/Core/Public/Internationalization/Regex.h>


void AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(UObject* ContextObject, TArray<UAerodynamicSurfaceSC*> Surfaces)
{
	if (!ContextObject)
	{
		UE_LOG(LogTemp, Error, TEXT("Context is missing!"));
		return;
	}
	FString PathToProfileFile;
	CalculatePolar(PathToProfileFile, 0.f, 0.f);
	/*FString AiroplaneFolderName = GetAiroplaneFolderName(ContextObject);

	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		FString SanitizedSurfaceName = Surface->GetName().Replace(TEXT(" "), TEXT("_"));
		FString AiroplaneSurfaceFolderPath = FPaths::Combine(TEXT("/Game/Airplane"), AiroplaneFolderName, TEXT("Aerodynamic"), SanitizedSurfaceName);
		FString PathToProfileFile = FindPathToProfile(Surface);
	
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		
		TArray<FAerodynamicSurfaceStructure> SubSurfaces = Surface->SurfaceForm;
		for (int i = 0; i < SubSurfaces.Num(); i++) {
			FAerodynamicSurfaceStructure SubSurface = SubSurfaces[i];
			FString AssetName = FString::Printf(TEXT("SubSurface_%d_ProfileAndFlap"), i);

			UScriptStruct* RowStruct = FTableRowBase::StaticStruct();
			UDataTableFactory* DataTableFactory = NewObject<UDataTableFactory>();
			DataTableFactory->Struct = RowStruct;

			UObject* DataTableAsset = AssetToolsModule.Get().CreateAsset(
				AssetName,
				AiroplaneSurfaceFolderPath,
				UDataTable::StaticClass(),
				DataTableFactory
			);

			UDataTable* ProfileAndFlapAsset = Cast<UDataTable>(DataTableAsset);
			if (!ProfileAndFlapAsset) {
				UE_LOG(LogTemp, Error, TEXT("Не вдалося створити UDataTable асет '%s' у шляху '%s'."), *AssetName, *AiroplaneSurfaceFolderPath);
				continue;
			}

			ProfileAndFlapAsset->RowStruct = FAerodynamicProfileRow::StaticStruct();

			for (int FlapAngle = SubSurface.MinFlapAngle; FlapAngle <= SubSurface.MaxFlapAngle; FlapAngle++)
			{
				FAerodynamicProfileRow ProfileForFlapConfig;
				TMap<float, PolarRow> Polar = CalculatePolar(PathToProfileFile, SubSurface.FlapPosition, FlapAngle);
				TMap<float, PolarRow> Polar;
				ProfileForFlapConfig.FlapAngle = FlapAngle;

				for (const TPair<float, PolarRow>& PolarConfig : Polar)
				{
					float Angle = PolarConfig.Key;
					const PolarRow& RowData = PolarConfig.Value;
					ProfileForFlapConfig.ClVsAoA.GetRichCurve()->AddKey(Angle, RowData.CL);
					ProfileForFlapConfig.CdVsAoA.GetRichCurve()->AddKey(Angle, RowData.CD);
					ProfileForFlapConfig.CmVsAoA.GetRichCurve()->AddKey(Angle, RowData.CM);
				}

				FName RowName = FName(*FString::Printf(TEXT("FlapAngle_%d"), FlapAngle));
				ProfileAndFlapAsset->AddRow(RowName, ProfileForFlapConfig);
			}

			UPackage* AssetPackage = ProfileAndFlapAsset->GetOutermost();
			AssetPackage->MarkPackageDirty();
			FString PackageFileName = FPackageName::LongPackageNameToFilename(AssetPackage->GetName(), FPackageName::GetAssetPackageExtension());
			bool bSaved = UPackage::SavePackage(AssetPackage, nullptr, RF_Public | RF_Standalone, *PackageFileName);

			if (bSaved)
			{
				UE_LOG(LogTemp, Log, TEXT("Асет '%s' успішно збережено."), *ProfileAndFlapAsset->GetPathName());
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Не вдалося зберегти асет '%s' на диск."), *ProfileAndFlapAsset->GetPathName());
			}
		}
	}*/
}



FString AerodynamicPhysicalCalculationUtil::FindPathToProfile(UAerodynamicSurfaceSC* Surface)
{
	FString ProfilePath = Surface->Profile->GetPathName();
	ProfilePath.RemoveFromStart("/Game/");
	ProfilePath = TextUtil::RemoveAfterSymbol(ProfilePath, '/');

	ProfilePath = FPaths::ProjectContentDir() + ProfilePath;
	IFileManager& FileManager = IFileManager::Get();

	TArray<FString> FoundFiles;
	FString FileExtension = TEXT("*.dat");

	FileManager.FindFiles(FoundFiles, *ProfilePath, *FileExtension);

	if (FoundFiles.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No .dat file associated with profile. Directory: %s"), *ProfilePath);
		return FString();
	}

	if (FoundFiles.Num() > 1)
	{
		UE_LOG(LogTemp, Error, TEXT("More than one .dat file associated with profile. Directory: %s"), *ProfilePath);
		return FString();
	}

	return FPaths::Combine(ProfilePath, FoundFiles[0]);
}

TMap<float, PolarRow> AerodynamicPhysicalCalculationUtil::CalculatePolar(FString PathToProfile, float FlapPosition, int FlapAngle)
{
	const FString OpenVSPScriptPath = FPaths::ProjectDir() + TEXT("Tools/OpenVSP/openvsp_vspaero.py");
	
	const FString OpenVSPCommand = FString::Printf(
		TEXT("\"%s\""),
		*OpenVSPScriptPath
	);

	ExecutePythonScript(OpenVSPCommand);

	/*const FString AirfoilScriptPath = FPaths::ProjectDir() + TEXT("Tools/Airfoil/airfoil.py");
	const FString XFoilScriptPath = FPaths::ProjectDir() + TEXT("Tools/XFoil/xfoil.py");

	const FString TmpDirPath = GetOrCreateTempWorkDirectory();
	const FString XFoilPath = ToAbsolutePath(FPaths::ProjectDir() + TEXT("Tools/XFoil/xfoil.exe"));
	const FString WingProfileSourcePath = ToAbsolutePath(PathToProfile);
	const FString ProfileDataPath = CopyToTempWorkDirectory(WingProfileSourcePath, "profile.dat");
	const FString OutputPolarPath = ToAbsolutePath(TmpDirPath + TEXT("/polar_results.dat"));
	const FString ExtrapolatedPolarPath = TmpDirPath + TEXT("/extrapolated_polar.dat");

	const FString XFoilCommand = FString::Printf(
		TEXT("\"%s\" \"%s\" \"%s\" \"%s\" \"%f\" \"%d\""),
		*XFoilScriptPath,
		*XFoilPath,
		*ProfileDataPath,
		*OutputPolarPath,
		FlapPosition,
		FlapAngle
	);

	ExecutePythonScript(XFoilCommand);

	TMap<float, PolarRow> Polar = ReadPolarFile(OutputPolarPath, 0, 1, 2, 4);
	const FString AirfoilDataPath = SavePolarForAirfoil(Polar);

	const FString AirfoilCommand = FString::Printf(
		TEXT("\"%s\" \"%s\" \"%s\""),
		*AirfoilScriptPath,
		*AirfoilDataPath,
		*ExtrapolatedPolarPath
	);

	ExecutePythonScript(AirfoilCommand);

	TMap<float, PolarRow> ExtrapolatedPolar = ReadPolarFile(ExtrapolatedPolarPath, 0, 1, 2, 3);

	CleanTempWorkDirectory();*/

	TMap<float, PolarRow> ExtrapolatedPolar;
	return ExtrapolatedPolar;
}

FString AerodynamicPhysicalCalculationUtil::GetAiroplaneFolderName(UObject* ContextObject)
{
	UClass* ObjectClass = ContextObject->GetClass();
	UPackage* AiroplanePackage = ObjectClass->GetPackage();
	FString AiroplanePackageName = AiroplanePackage->GetName();
	AiroplanePackageName.RemoveFromStart("/Game/Airplane/");
	return TextUtil::RemoveAfterSymbol(AiroplanePackageName, '/');
}

FString AerodynamicPhysicalCalculationUtil::GetOrCreateTempWorkDirectory()
{
	const int PathLen = 45;
	IFileManager& FileManager = IFileManager::Get();
	FString Path = FileManager.ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());
	while (Path.Len() > PathLen) {
		Path = TextUtil::RemoveAfterSymbol(Path, '/');
	}
	Path = FPaths::Combine(Path, TEXT("us_tmp"));
	if (!FileManager.DirectoryExists(*Path))
	{
		FileManager.MakeDirectory(*Path, false);
	}
	return Path;
}

bool AerodynamicPhysicalCalculationUtil::CleanTempWorkDirectory()
{
	FString Path = GetOrCreateTempWorkDirectory();
	return IFileManager::Get().DeleteDirectory(*Path, false, true);
}

FString AerodynamicPhysicalCalculationUtil::ToAbsolutePath(FString Path)
{
	return IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Path);
}

TMap<float, PolarRow> AerodynamicPhysicalCalculationUtil::ReadPolarFile(FString Path, int AoAIndex, int ClIndex, int CdIndex, int CmIndex)
{
	const FString PatternString = TEXT("^(\\s*-?\\d+\\.\\d+\\s*)+$");
	const FRegexPattern RegexPattern(PatternString);

	TMap<float, PolarRow> Polar;
	FString PolarFileContent;
	if (FFileHelper::LoadFileToString(PolarFileContent, *Path))
	{
		TArray<FString> Lines;
		PolarFileContent.ParseIntoArrayLines(Lines, true);

		TArray<FString> FilteredLines;
		bool IsStartData = false;

		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(RegexPattern, Line);
			if (Matcher.FindNext())
			{
				FString NormalizeLine = Line.TrimStartAndEnd();
				NormalizeLine.ReplaceCharInline('	', ' ');
				TArray<FString> Parts;
				NormalizeLine.ParseIntoArray(Parts, TEXT(" "), true);

				PolarRow Row;
				Row.CL = FCString::Atof(*Parts[ClIndex]);
				Row.CD = FCString::Atof(*Parts[CdIndex]);
				Row.CM = FCString::Atof(*Parts[CmIndex]);
				Polar.Add(FCString::Atof(*Parts[AoAIndex]), Row);
			}
		}
	}
	return Polar;
}

FString AerodynamicPhysicalCalculationUtil::SavePolarForAirfoil(TMap<float, PolarRow> Polar)
{
	FString AirfoilContent = "! \"Airfoil, OSU data at Re = .75 Million, Clean roughness\"\n1 NumTabs         \n0.75 Re\n";

	TArray <float> SortedKeys;
	Polar.GetKeys(SortedKeys);
	SortedKeys.Sort();

	for (float Key : SortedKeys)
	{
		PolarRow Row = Polar.FindChecked(Key);
		AirfoilContent += FString::Printf(TEXT("%f   %f   %f   %f\n"), Key, Row.CL, Row.CD, Row.CM);
	}

	FString WorkDir = GetOrCreateTempWorkDirectory();
	FString AirfoilPath = FPaths::Combine(WorkDir, TEXT("airfoil.dat"));
	FFileHelper::SaveStringToFile(AirfoilContent, *AirfoilPath);
	return AirfoilPath;
}

bool AerodynamicPhysicalCalculationUtil::ExecutePythonScript(FString Command)
{
	FPythonCommandEx PythonCommand;
	PythonCommand.Command = Command;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;

	const bool bSuccessAsFile = IPythonScriptPlugin::Get()->ExecPythonCommandEx(PythonCommand);
	if (PythonCommand.LogOutput.Num() > 0)
	{
		for (const FPythonLogOutputEntry& LogEntry : PythonCommand.LogOutput) {
			if (LogEntry.Type == EPythonLogOutputType::Error) {
				UE_LOG(LogTemp, Error, TEXT("[Python ERROR] %s"), *LogEntry.Output);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[Python INFO] %s"), *LogEntry.Output);
			}
		}
	}
	return bSuccessAsFile;
}

FString AerodynamicPhysicalCalculationUtil::CopyToTempWorkDirectory(FString SourcePath, FString Name)
{
	FString WorkDir = GetOrCreateTempWorkDirectory();
	FString DestinationPath = FPaths::Combine(WorkDir, Name);
	IFileManager& FileManager = IFileManager::Get();
	FileManager.Copy(*DestinationPath, *SourcePath);
	return DestinationPath;
}