#include "EnvironmentSectionWidget.h"
#include "CesiumGeoreference.h"
#include "CesiumSunSky.h"
#include "Cesium3DTileset.h"
#include "GameFramework/Actor.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Components/Button.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Save/EnvironmentSettingsSave.h"
#include "UAVSimulator/Actor/EnvironmentActorManager.h"
#include "UAVSimulator/Actor/RainEffectManager.h"
#include "UAVSimulator/UAVSimulator.h"

const FString UEnvironmentSectionWidget::EnvironmentSaveSlotName = TEXT("EnvironmentSettings");

void UEnvironmentSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SpinBoxOriginLatitude->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginLatitudeCommitted);
	SpinBoxOriginLongitude->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginLongitudeCommitted);
	SpinBoxOriginHeight->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginHeightCommitted);
	SpinBoxTimeZone->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnTimeZoneCommitted);
	SpinBoxSolarTime->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnSolarTimeCommitted);
	TerrainSurfaceCB->OnCheckStateChanged.AddDynamic(this, &UEnvironmentSectionWidget::OnTerrainSurfaceChanged);

	if (ConfigurateEnvActorsBtn)
		ConfigurateEnvActorsBtn->OnClicked.AddDynamic(this, &UEnvironmentSectionWidget::OnConfigurateEnvActorsBtnClicked);

	if (SpinBoxRainIntensity)
		SpinBoxRainIntensity->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnRainIntensityCommitted);

	LoadAndApplySavedSettings();
	SyncFromWorld();
}

void UEnvironmentSectionWidget::OnSectionActivated_Implementation()
{
	SyncFromWorld();
}

void UEnvironmentSectionWidget::SyncFromWorld()
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
	{
		SpinBoxOriginLatitude->SetValue((float)Geo->GetOriginLatitude());
		SpinBoxOriginLongitude->SetValue((float)Geo->GetOriginLongitude());
		SpinBoxOriginHeight->SetValue((float)Geo->GetOriginHeight());
	}

	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SpinBoxTimeZone->SetValue((float)SunSky->TimeZone);
		SpinBoxSolarTime->SetValue((float)SunSky->SolarTime);
	}

	if (ACesium3DTileset* Tileset = GetTileset())
	{
		const bool bEnabled = !Tileset->IsHidden();
		TerrainSurfaceCB->SetIsChecked(bEnabled);
		ApplyTerrainSurfaceState(bEnabled);
	}

	if (SpinBoxRainIntensity)
	{
		if (ARainEffectManager* Manager = GetRainEffectManager())
			SpinBoxRainIntensity->SetValue(Manager->GetRainIntensity());
	}
}

void UEnvironmentSectionWidget::OnConfigurateEnvActorsBtnClicked()
{
	if (AEnvironmentActorManager* Manager = GetEnvironmentActorManager())
		Manager->OpenConfigurationTool();
}

void UEnvironmentSectionWidget::OnOriginLatitudeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
		Geo->SetOriginLatitude((double)Value);
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnOriginLongitudeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
		Geo->SetOriginLongitude((double)Value);
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnOriginHeightCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
		Geo->SetOriginHeight((double)Value);
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnTimeZoneCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->TimeZone = (double)Value;
		SunSky->UpdateSun();
		UpdateNightVisuals(SunSky);
	}
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnSolarTimeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->SolarTime = (double)Value;
		SunSky->UpdateSun();
		UpdateNightVisuals(SunSky);
	}
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::UpdateNightVisuals(ACesiumSunSky* SunSky) const
{
	if (!SunSky || !SunSky->DirectionalLight)
		return;

	TInlineComponentArray<UDirectionalLightComponent*> Lights;
	SunSky->GetComponents(Lights);

	UDirectionalLightComponent* Moon = nullptr;
	for (UDirectionalLightComponent* Light : Lights)
	{
		if (Light != SunSky->DirectionalLight && Light->AtmosphereSunLightIndex == 1)
		{
			Moon = Light;
			break;
		}
	}
	if (Moon)
	{
		// Дзеркальний до сонця напрямок: інверсія forward-вектора через (-Pitch, Yaw+180).
		const FRotator SunRotation = SunSky->DirectionalLight->GetRelativeRotation();
		Moon->SetRelativeRotation(FRotator(-SunRotation.Pitch, SunRotation.Yaw + 180.0, SunRotation.Roll));
	}

	// StarsSphere (SM_SkySphere + MI_Stars, додається вручну в CesiumSunSky_0) — яскравість
	// зірок (NightFactor) керується напряму нахилом сонця, а не часом доби, щоб коректно
	// узгоджуватись із фактичним затемненням атмосфери (SkyAtmosphere.transmittanceMinLightElevationAngle).
	TInlineComponentArray<UStaticMeshComponent*> Meshes;
	SunSky->GetComponents(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetName() != TEXT("StarsSphere"))
			continue;

		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		if (!MID)
			MID = Mesh->CreateDynamicMaterialInstance(0);

		if (MID)
		{
			// ACesiumSunSky::Elevation є protected (лише BlueprintReadOnly), тож читаємо
			// через рефлексію — той самий підхід, що вже застосовується нижче в
			// ApplyTerrainSurfaceState для стороннього skybox-класу.
			double Elevation = 0.0;
			static const FName ElevationName(TEXT("Elevation"));
			if (FDoubleProperty* ElevationProp = FindFProperty<FDoubleProperty>(SunSky->GetClass(), ElevationName))
				Elevation = ElevationProp->GetPropertyValue_InContainer(SunSky);

			// Повний нуль на 10° над горизонтом, повна яскравість на 10° під горизонтом
			// (приблизно морські сутінки) — узгоджено з transmittanceMinLightElevationAngle=-90.
			const double NightFactor = FMath::Clamp(-Elevation / 10.0, 0.0, 1.0);
			MID->SetScalarParameterValue(TEXT("NightFactor"), NightFactor);
		}
		break;
	}
}

void UEnvironmentSectionWidget::OnTerrainSurfaceChanged(bool bIsChecked)
{
	ApplyTerrainSurfaceState(bIsChecked);
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnRainIntensityCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ARainEffectManager* Manager = GetRainEffectManager())
		Manager->SetRainIntensity(Value);
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::ApplyTerrainSurfaceState(bool bEnabled)
{
	if (ACesium3DTileset* Tileset = GetTileset())
	{
		Tileset->SetActorHiddenInGame(!bEnabled);
		Tileset->SetActorEnableCollision(bEnabled);
		Tileset->SetActorTickEnabled(bEnabled);
	}

	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->SetActorHiddenInGame(!bEnabled);
		SunSky->SetActorTickEnabled(bEnabled);
	}

	UWorld* World = GetWorld();
	if (!World)
		return;

	if (bEnabled)
	{
		if (SpawnedDefaultSkybox)
		{
			SpawnedDefaultSkybox->Destroy();
			SpawnedDefaultSkybox = nullptr;
		}
		if (SpawnedDefaultSun)
		{
			SpawnedDefaultSun->Destroy();
			SpawnedDefaultSun = nullptr;
		}
	}
	else
	{
		if (!SpawnedDefaultSkybox && DefaultSkyboxClass)
		{
			SpawnedDefaultSkybox = World->SpawnActor<AActor>(DefaultSkyboxClass);

			static const FName ColorsDeterminedBySunPositionName(TEXT("Colors Determined By Sun Position"));
			if (FBoolProperty* ColorsBySunProp = SpawnedDefaultSkybox
				? FindFProperty<FBoolProperty>(SpawnedDefaultSkybox->GetClass(), ColorsDeterminedBySunPositionName)
				: nullptr)
			{
				ColorsBySunProp->SetPropertyValue_InContainer(SpawnedDefaultSkybox, false);
			}
		}
		if (!SpawnedDefaultSun && DefaultSunClass)
			SpawnedDefaultSun = World->SpawnActor<AActor>(DefaultSunClass);
	}
}

void UEnvironmentSectionWidget::LoadAndApplySavedSettings()
{
	UEnvironmentSettingsSave* Save = Cast<UEnvironmentSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(EnvironmentSaveSlotName, /*UserIndex=*/0));
	if (!Save)
		return;

	if (ACesiumGeoreference* Geo = GetGeoreference())
	{
		// Один атомарний виклик замість трьох послідовних SetOrigin* — уникає проміжного
		// перерахунку georeference зі старими значеннями двох ще не застосованих полів.
		Geo->SetOriginLongitudeLatitudeHeight(
			FVector(Save->OriginLongitude, Save->OriginLatitude, Save->OriginHeight));
	}

	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->TimeZone  = Save->TimeZone;
		SunSky->SolarTime = Save->SolarTime;
		SunSky->UpdateSun();
		UpdateNightVisuals(SunSky);
	}

	ApplyTerrainSurfaceState(Save->bTerrainSurfaceEnabled);

	if (ARainEffectManager* Manager = GetRainEffectManager())
	{
		Manager->SetRainIntensity((float)Save->RainIntensity);
	}
}

void UEnvironmentSectionWidget::SaveCurrentSettings()
{
	UEnvironmentSettingsSave* Save = Cast<UEnvironmentSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UEnvironmentSettingsSave::StaticClass()));

	if (ACesiumGeoreference* Geo = GetGeoreference())
	{
		Save->OriginLatitude  = Geo->GetOriginLatitude();
		Save->OriginLongitude = Geo->GetOriginLongitude();
		Save->OriginHeight    = Geo->GetOriginHeight();
	}

	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		Save->TimeZone  = SunSky->TimeZone;
		Save->SolarTime = SunSky->SolarTime;
	}

	if (ACesium3DTileset* Tileset = GetTileset())
	{
		Save->bTerrainSurfaceEnabled = !Tileset->IsHidden();
	}

	if (ARainEffectManager* Manager = GetRainEffectManager())
	{
		Save->RainIntensity = Manager->GetRainIntensity();
	}

	UGameplayStatics::SaveGameToSlot(Save, EnvironmentSaveSlotName, /*UserIndex=*/0);
}

ACesiumGeoreference* UEnvironmentSectionWidget::GetGeoreference() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	TArray<AActor*> AllGeoreferences;
	UGameplayStatics::GetAllActorsOfClass(World, ACesiumGeoreference::StaticClass(), AllGeoreferences);
	if (AllGeoreferences.Num() > 1)
	{
		UE_LOG(LogUAV, Warning, TEXT("EnvironmentSectionWidget::GetGeoreference: %d ACesiumGeoreference actors found in level (expected 1) — using the first one, which may NOT be the one your map/tileset actually uses:"), AllGeoreferences.Num());
		for (AActor* A : AllGeoreferences)
		{
			if (ACesiumGeoreference* G = Cast<ACesiumGeoreference>(A))
				UE_LOG(LogUAV, Warning, TEXT("  - %s OriginLLH=(%f,%f,%f)"), *G->GetName(), G->GetOriginLongitude(), G->GetOriginLatitude(), G->GetOriginHeight());
		}
	}

	return AllGeoreferences.Num() > 0 ? Cast<ACesiumGeoreference>(AllGeoreferences[0]) : nullptr;
}

ACesiumSunSky* UEnvironmentSectionWidget::GetSunSky() const
{
	if (UWorld* World = GetWorld())
		return Cast<ACesiumSunSky>(UGameplayStatics::GetActorOfClass(World, ACesiumSunSky::StaticClass()));
	return nullptr;
}

ACesium3DTileset* UEnvironmentSectionWidget::GetTileset() const
{
	if (UWorld* World = GetWorld())
		return Cast<ACesium3DTileset>(UGameplayStatics::GetActorOfClass(World, ACesium3DTileset::StaticClass()));
	return nullptr;
}

AEnvironmentActorManager* UEnvironmentSectionWidget::GetEnvironmentActorManager() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	if (AEnvironmentActorManager* Existing = Cast<AEnvironmentActorManager>(
			UGameplayStatics::GetActorOfClass(World, AEnvironmentActorManager::StaticClass())))
	{
		return Existing;
	}

	// У рівні ще немає розміщеного менеджера — створюємо один, інакше кнопці нема кого викликати.
	// EWZoneActorClass лишиться незаданим, доки його не признать вручну в редакторі.
	AEnvironmentActorManager* Spawned = World->SpawnActor<AEnvironmentActorManager>();
	UE_LOG(LogUAV, Log, TEXT("EnvironmentSectionWidget::GetEnvironmentActorManager: no AEnvironmentActorManager in level — spawned %s"),
		Spawned ? *Spawned->GetName() : TEXT("FAILED"));
	return Spawned;
}

ARainEffectManager* UEnvironmentSectionWidget::GetRainEffectManager() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	if (ARainEffectManager* Existing = Cast<ARainEffectManager>(
			UGameplayStatics::GetActorOfClass(World, ARainEffectManager::StaticClass())))
	{
		return Existing;
	}

	// У рівні ще немає розміщеного менеджера — спінбоксу нема кого перемикати. RainSystem
	// лишиться незаданим, доки його не признать вручну в редакторі (як EWZoneActorClass вище).
	ARainEffectManager* Spawned = World->SpawnActor<ARainEffectManager>();
	UE_LOG(LogUAV, Log, TEXT("EnvironmentSectionWidget::GetRainEffectManager: no ARainEffectManager in level — spawned %s"),
		Spawned ? *Spawned->GetName() : TEXT("FAILED"));
	return Spawned;
}
