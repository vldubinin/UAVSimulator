#include "AerodynamicPhysicalCalculationUtil.h"
#include "Misc/Paths.h"
#include "IPythonScriptPlugin.h"
#include "HAL/PlatformFileManager.h"
#include "PythonScriptTypes.h" // Потрібно для EPythonLogOutputType
#include "Misc/FileHelper.h"
#include <Runtime/Core/Public/Internationalization/Regex.h>
#include <Runtime/Core/Public/Internationalization/Regex.h>


void AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(UObject* ContextObject, TArray<UAerodynamicSurfaceSC*> Surfaces)
{
	if (!ContextObject)
	{
		UE_LOG(LogTemp, Error, TEXT("Context is missing!"));
		return;
	}

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UAerodynamicProfileDataAsset::StaticClass();
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	FString AiroplaneFolderName = GetAiroplaneFolderName(ContextObject);

	for (UAerodynamicSurfaceSC* Surface : Surfaces) {
		FString AiroplaneSurfaceFolderPath = FPaths::Combine(TEXT("/Game/Airplane"), AiroplaneFolderName, TEXT("Aerodynamic"), Surface->GetName());
		FString PathToProfileFile = FindPathToProfile(Surface);
		for (int FlapAngle = (Surface->MaxFlapAngle * -1); FlapAngle <= Surface->MaxFlapAngle; FlapAngle++) {
			TMap<float, PolarRow> Res = CalculatePolar(PathToProfileFile, Surface->FlapPosition, FlapAngle);

			FString PolarAssetName = FString::Printf(TEXT("PolarFlap_%d"), FlapAngle);
			UAerodynamicProfileDataAsset* NewPolarAsset = Cast<UAerodynamicProfileDataAsset>(
				AssetToolsModule.Get().CreateAsset(PolarAssetName, AiroplaneSurfaceFolderPath, UAerodynamicProfileDataAsset::StaticClass(), Factory)
			);
			if (NewPolarAsset)
			{
				UE_LOG(LogTemp, Warning, TEXT("Асет успішно створено за шляхом: %s"), *NewPolarAsset->GetPathName());
				UPackage* Package = NewPolarAsset->GetOutermost();
				Package->MarkPackageDirty();

				FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
				bool bSaved = UPackage::SavePackage(Package, nullptr, RF_Public | RF_Standalone, *PackageFileName);
				if (bSaved)
				{
					UE_LOG(LogTemp, Warning, TEXT("Асет успішно збережено на диск."));
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("Не вдалося зберегти асет на диск."));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Не вдалося створити асет."));
			}
		}
	}
}

FString AerodynamicPhysicalCalculationUtil::FindPathToProfile(UAerodynamicSurfaceSC* Surface)
{
	FString ProfilePath = Surface->AerodynamicProfile->Profile->GetPathName();
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
	const FString AirfoilScriptPath = FPaths::ProjectDir() + TEXT("Tools/Airfoil/airfoil.py");
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

	CleanTempWorkDirectory();

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