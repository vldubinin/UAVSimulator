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


void AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(UObject* ContextObject, TArray<UAerodynamicSurfaceSC*> Surfaces)
{
	if (!ContextObject)
	{
		//UE_LOG(LogTemp, Error, TEXT("Context is missing!"));
		return;
	}
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		FString PathToProfileFile = FindPathToProfile(Surface);
		Surface->Profile->GetPackage();
		TArray<FAerodynamicSurfaceStructure> SubSurfaces = Surface->SurfaceForm;
		for (int i = 0; i < SubSurfaces.Num() - 1 && SubSurfaces.Num() > 1; i++) {
			FAerodynamicSurfaceStructure RootSurface = SubSurfaces[i];
			int DeflectionAngleStart = RootSurface.MinFlapAngle;
			int DeflectionAngleEnd = RootSurface.MaxFlapAngle;
			int RootChord = RootSurface.ChordSize;
			int Sweep = 0; // наклон
			FAerodynamicSurfaceStructure TipSurface = SubSurfaces[i + 1];
			int TipChord = TipSurface.ChordSize;
			int Span = TipSurface.Offset.Y;
			CalculatePolar(PathToProfileFile, RootChord, TipChord, Span, DeflectionAngleStart, DeflectionAngleEnd, Sweep, *Surface->GetName(), i);
		}
	}
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
		//UE_LOG(LogTemp, Error, TEXT("No .dat file associated with profile. Directory: %s"), *ProfilePath);
		return FString();
	}

	if (FoundFiles.Num() > 1)
	{
		//UE_LOG(LogTemp, Error, TEXT("More than one .dat file associated with profile. Directory: %s"), *ProfilePath);
		return FString();
	}

	FString RelativePathToProfile = FPaths::Combine(ProfilePath, FoundFiles[0]);
	return IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RelativePathToProfile);
}

TMap<float, PolarRow> AerodynamicPhysicalCalculationUtil::CalculatePolar(FString PathToProfile, int RootChord, int TipChord, int Span, int DeflectionAngleStart, int DeflectionAngleEnd, int Sweep, FString SurfaceName, int SubSurfaceIndex)
{
	const FString OpenVSPScriptPath = FPaths::ProjectDir() + TEXT("Tools/OpenVSP/openvsp_vspaero.py");

	const FString OpenVSPCommand = FString::Printf(
		TEXT("\"%s\" \"%d\" \"%d\" \"%d\" \"%d\" \"%d\" \"%d\" \"%s\" \"%d\" \"%s\""),
		*OpenVSPScriptPath,
		RootChord,
		TipChord,
		Span,
		DeflectionAngleStart,
		DeflectionAngleEnd,
		Sweep,
		*SurfaceName,
		SubSurfaceIndex,
		*PathToProfile
	);

	ExecutePythonScript(OpenVSPCommand);

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