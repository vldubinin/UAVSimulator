#include "EnvironmentActorManager.h"
#include "UAVSimulator/Actor/EWZoneActor.h"
#include "UAVSimulator/Util/AerodynamicToolRunner.h"
#include "UAVSimulator/UAVSimulator.h"
#include "CesiumGeoreference.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	// Мають збігатися з ScriptPath/OUTPUT_FILE у Tools/ProjectTools/configurate_env_actors.py.
	const TCHAR* ConfigurationScriptRelativePath = TEXT("Tools/ProjectTools/configurate_env_actors.py");
	const TCHAR* ConfigurationOutputRelativePath = TEXT("Tools/ProjectTools/env_actors.json");
	const TCHAR* ElectronicWarfareArrayField      = TEXT("electronic_warfare");
}

AEnvironmentActorManager::AEnvironmentActorManager()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* DefaultRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRoot"));
	RootComponent = DefaultRoot;
}

void AEnvironmentActorManager::BeginPlay()
{
	Super::BeginPlay();

	// env_actors.json — стійке сховище EWConfigurations між запусками: перечитуємо його тут,
	// щоб конфігурація, збережена в попередній сесії (через SAVE у скрипті або редактор),
	// підхоплювалась і без повторного відкриття інструменту карти. Якщо файла ще нема (перший
	// запуск і жодного разу нічого не зберігали) — лишаємось на EWConfigurations, як задано
	// вручну в редакторі, і все одно спавнимо зони з нього.
	if (!LoadConfigurationsFromFile())
	{
		RefreshEWZones();
	}
}

void AEnvironmentActorManager::OpenConfigurationTool()
{
	const ACesiumGeoreference* Geo = GetGeoreference();
	const double Latitude  = Geo ? Geo->GetOriginLatitude()  : 0.0;
	const double Longitude = Geo ? Geo->GetOriginLongitude() : 0.0;

	const FString ScriptPath = FPaths::ProjectDir() + ConfigurationScriptRelativePath;

	// Порядок argv, який очікує configurate_env_actors.py: <latitude> <longitude>
	const FString Command = FString::Printf(
		TEXT("\"%s\" \"%f\" \"%f\""),
		*ScriptPath,
		Latitude,
		Longitude);

	UE_LOG(LogUAV, Log, TEXT("EnvironmentActorManager::OpenConfigurationTool: launching %s"), *Command);

	// Скрипт при старті підхоплює env_actors.json (якщо він є) і одразу відмальовує наявні
	// зони — записуємо туди поточний стан ПЕРЕД запуском, щоб карта відкрилась не порожньою.
	SaveConfigurationsToFile();

	// Синхронний виклик — блокується, доки користувач не закриє вікно карти. Щойно
	// повернулись, файл (якщо натискали SAVE у скрипті) уже містить найсвіжіші дані.
	AerodynamicToolRunner::RunPythonScript(Command);

	LoadConfigurationsFromFile();
}

void AEnvironmentActorManager::SaveConfigurationsToFile() const
{
	TArray<TSharedPtr<FJsonValue>> EWArray;
	for (const FEWZoneConfiguration& Config : EWConfigurations)
	{
		const TSharedPtr<FJsonObject> EWObject = MakeShared<FJsonObject>();
		EWObject->SetNumberField(TEXT("latitude"), Config.Latitude);
		EWObject->SetNumberField(TEXT("longitude"), Config.Longitude);
		EWObject->SetNumberField(TEXT("height"), Config.Height);
		EWObject->SetNumberField(TEXT("radius"), Config.Radius);
		EWArray.Add(MakeShared<FJsonValueObject>(EWObject));
	}

	const TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetArrayField(ElectronicWarfareArrayField, EWArray);

	FString JsonString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	const FString FilePath = FPaths::ProjectDir() + ConfigurationOutputRelativePath;
	if (!FFileHelper::SaveStringToFile(JsonString, *FilePath))
	{
		UE_LOG(LogUAV, Warning, TEXT("EnvironmentActorManager::SaveConfigurationsToFile: failed to write %s"), *FilePath);
		return;
	}

	UE_LOG(LogUAV, Log, TEXT("EnvironmentActorManager::SaveConfigurationsToFile: wrote %d EW configuration(s) to %s"),
		EWConfigurations.Num(), *FilePath);
}

bool AEnvironmentActorManager::LoadConfigurationsFromFile()
{
	const FString FilePath = FPaths::ProjectDir() + ConfigurationOutputRelativePath;

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogUAV, Warning, TEXT("EnvironmentActorManager::LoadConfigurationsFromFile: could not read %s (normal if nothing was ever saved yet)"), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogUAV, Warning, TEXT("EnvironmentActorManager::LoadConfigurationsFromFile: failed to parse %s"), *FilePath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* EWArray = nullptr;
	if (!RootObject->TryGetArrayField(ElectronicWarfareArrayField, EWArray))
	{
		UE_LOG(LogUAV, Warning, TEXT("EnvironmentActorManager::LoadConfigurationsFromFile: %s has no '%s' array"), *FilePath, ElectronicWarfareArrayField);
		return false;
	}

	EWConfigurations.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *EWArray)
	{
		const TSharedPtr<FJsonObject>* EWObject = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(EWObject))
			continue;

		FEWZoneConfiguration Config;
		(*EWObject)->TryGetNumberField(TEXT("latitude"), Config.Latitude);
		(*EWObject)->TryGetNumberField(TEXT("longitude"), Config.Longitude);
		(*EWObject)->TryGetNumberField(TEXT("height"), Config.Height);
		(*EWObject)->TryGetNumberField(TEXT("radius"), Config.Radius);

		EWConfigurations.Add(Config);
	}

	UE_LOG(LogUAV, Log, TEXT("EnvironmentActorManager::LoadConfigurationsFromFile: loaded %d EW configuration(s) from %s"),
		EWConfigurations.Num(), *FilePath);

	RefreshEWZones();
	return true;
}

void AEnvironmentActorManager::RefreshEWZones()
{
	for (AEWZoneActor* Zone : SpawnedEWZones)
	{
		if (Zone)
			Zone->Destroy();
	}
	SpawnedEWZones.Reset();

	if (!EWZoneActorClass)
	{
		UE_LOG(LogUAV, Warning, TEXT("EnvironmentActorManager::RefreshEWZones: EWZoneActorClass is not set — nothing spawned"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
		return;

	for (const FEWZoneConfiguration& Config : EWConfigurations)
	{
		AEWZoneActor* Zone = World->SpawnActor<AEWZoneActor>(EWZoneActorClass, GetActorTransform());
		if (!Zone)
			continue;

		// Метри -> сантиметри (Unreal-одиниці) — AEWZoneActor::Radius очікує см.
		Zone->SetGeoPosition(Config.Longitude, Config.Latitude, Config.Height);
		Zone->SetRadius(Config.Radius * 100.0f);

		SpawnedEWZones.Add(Zone);
	}
}

#if WITH_EDITOR
void AEnvironmentActorManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedProperty = PropertyChangedEvent.GetMemberPropertyName();
	if (ChangedProperty == GET_MEMBER_NAME_CHECKED(AEnvironmentActorManager, EWConfigurations) ||
		ChangedProperty == GET_MEMBER_NAME_CHECKED(AEnvironmentActorManager, EWZoneActorClass))
	{
		RefreshEWZones();

		// Ручні правки EWConfigurations в редакторі теж мають пережити наступний запуск —
		// зберігаємо їх у той самий файл, з якого сесія стартує LoadConfigurationsFromFile().
		if (ChangedProperty == GET_MEMBER_NAME_CHECKED(AEnvironmentActorManager, EWConfigurations))
		{
			SaveConfigurationsToFile();
		}
	}
}
#endif

ACesiumGeoreference* AEnvironmentActorManager::GetGeoreference() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	return Cast<ACesiumGeoreference>(UGameplayStatics::GetActorOfClass(World, ACesiumGeoreference::StaticClass()));
}
