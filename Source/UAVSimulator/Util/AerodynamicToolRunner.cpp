#include "AerodynamicToolRunner.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/TextUtil.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"
#include <Runtime/Core/Public/Internationalization/Regex.h>

FString AerodynamicToolRunner::GetOrCreateWorkDir()
{
	const int32 PathLen = 45;
	IFileManager& FileManager = IFileManager::Get();
	FString Path = FileManager.ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());

	// Скорочуємо шлях до 45 символів щоб уникнути обмежень командного рядка
	while (Path.Len() > PathLen)
	{
		Path = TextUtil::RemoveAfterSymbol(Path, '/');
	}
	Path = FPaths::Combine(Path, TEXT("us_tmp"));

	// Створюємо директорію якщо вона ще не існує
	if (!FileManager.DirectoryExists(*Path))
	{
		FileManager.MakeDirectory(*Path, false);
	}
	return Path;
}

bool AerodynamicToolRunner::CleanWorkDir()
{
	FString Path = GetOrCreateWorkDir();
	// Видаляємо рекурсивно (true) але не лише вміст — разом з самою директорією
	return IFileManager::Get().DeleteDirectory(*Path, false, true);
}

FString AerodynamicToolRunner::ToAbsolutePath(FString Path)
{
	return IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Path);
}

FString AerodynamicToolRunner::CopyToWorkDir(FString SourcePath, FString Name)
{
	FString WorkDir = GetOrCreateWorkDir();
	FString DestinationPath = FPaths::Combine(WorkDir, Name);
	IFileManager::Get().Copy(*DestinationPath, *SourcePath);
	return DestinationPath;
}

FString AerodynamicToolRunner::SavePolarFile(TMap<float, FPolarRow> Polar)
{
	// Заголовок у форматі airfoilprep (утиліта Viterna-екстраполяції)
	FString Content = TEXT("! \"Airfoil, OSU data at Re = .75 Million, Clean roughness\"\n1 NumTabs         \n0.75 Re\n");

	// Сортуємо за кутом атаки для коректного відображення у файлі
	TArray<float> SortedKeys;
	Polar.GetKeys(SortedKeys);
	SortedKeys.Sort();

	for (float Key : SortedKeys)
	{
		FPolarRow Row = Polar.FindChecked(Key);
		Content += FString::Printf(TEXT("%f   %f   %f   %f\n"), Key, Row.CL, Row.CD, Row.CM);
	}

	FString AirfoilPath = FPaths::Combine(GetOrCreateWorkDir(), TEXT("airfoil.dat"));
	FFileHelper::SaveStringToFile(Content, *AirfoilPath);
	return AirfoilPath;
}

bool AerodynamicToolRunner::RunPythonScript(FString Command)
{
	FPythonCommandEx PythonCommand;
	PythonCommand.Command = Command;
	// ExecuteFile — виконує файл скрипту, а не рядок коду
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;

	const bool bSuccess = IPythonScriptPlugin::Get()->ExecPythonCommandEx(PythonCommand);

	// Перенаправляємо весь вивід Python у LogUAV
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		if (Entry.Type == EPythonLogOutputType::Error)
		{
			UE_LOG(LogUAV, Error, TEXT("[Python ERROR] %s"), *Entry.Output);
		}
		else
		{
			UE_LOG(LogUAV, Log, TEXT("[Python INFO] %s"), *Entry.Output);
		}
	}
	return bSuccess;
}

TMap<float, FPolarRow> AerodynamicToolRunner::ParsePolarFile(FString Path, int32 AoAIdx, int32 ClIdx, int32 CdIdx, int32 CmIdx)
{
	// Регулярний вираз відбирає лише рядки з числовими даними (пропускає заголовки)
	const FRegexPattern RegexPattern(TEXT("^(\\s*-?\\d+\\.\\d+\\s*)+$"));
	TMap<float, FPolarRow> Polar;

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *Path)) return Polar;

	TArray<FString> Lines;
	FileContent.ParseIntoArrayLines(Lines, true);

	for (const FString& Line : Lines)
	{
		FRegexMatcher Matcher(RegexPattern, Line);
		if (!Matcher.FindNext()) continue;  // Пропускаємо нечислові рядки

		// Нормалізуємо роздільники і розбиваємо на стовпці
		FString Normalized = Line.TrimStartAndEnd();
		Normalized.ReplaceCharInline('\t', ' ');
		TArray<FString> Parts;
		Normalized.ParseIntoArray(Parts, TEXT(" "), true);

		FPolarRow Row;
		Row.CL = FCString::Atof(*Parts[ClIdx]);
		Row.CD = FCString::Atof(*Parts[CdIdx]);
		Row.CM = FCString::Atof(*Parts[CmIdx]);
		Polar.Add(FCString::Atof(*Parts[AoAIdx]), Row);
	}

	return Polar;
}

FString AerodynamicToolRunner::GetOwnerFolderName(UObject* ContextObject)
{
	// Формат пакету: "/Game/Airplane/<FolderName>/...", витягуємо перший сегмент після "/Game/Airplane/"
	FString PackageName = ContextObject->GetClass()->GetPackage()->GetName();
	PackageName.RemoveFromStart("/Game/Airplane/");
	return TextUtil::RemoveAfterSymbol(PackageName, '/');
}
