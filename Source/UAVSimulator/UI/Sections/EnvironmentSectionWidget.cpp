#include "EnvironmentSectionWidget.h"
#include "CesiumGeoreference.h"
#include "CesiumSunSky.h"
#include "Cesium3DTileset.h"
#include "GameFramework/Actor.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Save/EnvironmentSettingsSave.h"

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
	}
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnSolarTimeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->SolarTime = (double)Value;
		SunSky->UpdateSun();
	}
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnTerrainSurfaceChanged(bool bIsChecked)
{
	ApplyTerrainSurfaceState(bIsChecked);
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
		Geo->SetOriginLatitude(Save->OriginLatitude);
		Geo->SetOriginLongitude(Save->OriginLongitude);
		Geo->SetOriginHeight(Save->OriginHeight);
	}

	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->TimeZone  = Save->TimeZone;
		SunSky->SolarTime = Save->SolarTime;
		SunSky->UpdateSun();
	}

	ApplyTerrainSurfaceState(Save->bTerrainSurfaceEnabled);
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

	UGameplayStatics::SaveGameToSlot(Save, EnvironmentSaveSlotName, /*UserIndex=*/0);
}

ACesiumGeoreference* UEnvironmentSectionWidget::GetGeoreference() const
{
	if (UWorld* World = GetWorld())
		return Cast<ACesiumGeoreference>(UGameplayStatics::GetActorOfClass(World, ACesiumGeoreference::StaticClass()));
	return nullptr;
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
