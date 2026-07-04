#include "EnvironmentSectionWidget.h"
#include "CesiumGeoreference.h"
#include "CesiumSunSky.h"
#include "Cesium3DTileset.h"
#include "GameFramework/Actor.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Kismet/GameplayStatics.h"

void UEnvironmentSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SpinBoxOriginLatitude->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginLatitudeCommitted);
	SpinBoxOriginLongitude->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginLongitudeCommitted);
	SpinBoxOriginHeight->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginHeightCommitted);
	SpinBoxTimeZone->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnTimeZoneCommitted);
	SpinBoxSolarTime->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnSolarTimeCommitted);
	TerrainSurfaceCB->OnCheckStateChanged.AddDynamic(this, &UEnvironmentSectionWidget::OnTerrainSurfaceChanged);

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
}

void UEnvironmentSectionWidget::OnOriginLongitudeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
		Geo->SetOriginLongitude((double)Value);
}

void UEnvironmentSectionWidget::OnOriginHeightCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
		Geo->SetOriginHeight((double)Value);
}

void UEnvironmentSectionWidget::OnTimeZoneCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->TimeZone = (double)Value;
		SunSky->UpdateSun();
	}
}

void UEnvironmentSectionWidget::OnSolarTimeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->SolarTime = (double)Value;
		SunSky->UpdateSun();
	}
}

void UEnvironmentSectionWidget::OnTerrainSurfaceChanged(bool bIsChecked)
{
	ApplyTerrainSurfaceState(bIsChecked);
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
