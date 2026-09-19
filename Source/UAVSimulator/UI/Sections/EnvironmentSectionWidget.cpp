#include "EnvironmentSectionWidget.h"
#include "CesiumGeoreference.h"
#include "CesiumSunSky.h"
#include "Cesium3DTileset.h"
#include "GameFramework/Actor.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Save/EnvironmentSettingsSave.h"
#include "UAVSimulator/Actor/EnvironmentActorManager.h"
#include "UAVSimulator/Actor/RainEffectManager.h"
#include "UAVSimulator/Actor/StreetLightsManager.h"
#include "UAVSimulator/UAVSimulator.h"

const FString UEnvironmentSectionWidget::EnvironmentSaveSlotName = TEXT("EnvironmentSettings");

namespace
{
	// Рядки опцій ComboBoxStreetLightsDataSource <-> EStreetLightsDataSource.
	FString StreetLightsDataSourceToString(EStreetLightsDataSource Source)
	{
		return Source == EStreetLightsDataSource::Cesium ? TEXT("Cesium") : TEXT("Custom");
	}

	EStreetLightsDataSource StringToStreetLightsDataSource(const FString& Option)
	{
		return Option == TEXT("Cesium") ? EStreetLightsDataSource::Cesium : EStreetLightsDataSource::Custom;
	}
}

void UEnvironmentSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SpinBoxOriginLatitude->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginLatitudeCommitted);
	SpinBoxOriginLongitude->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginLongitudeCommitted);
	SpinBoxOriginHeight->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginHeightCommitted);
	SpinBoxTimeZone->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnTimeZoneCommitted);
	SpinBoxSolarTime->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnSolarTimeCommitted);
	SpinBoxStarsDensity->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnStarsDensityCommitted);
	SpinBoxStarsThreshold->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnStarsThresholdCommitted);
	SpinBoxStarsPointSize->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnStarsPointSizeCommitted);
	SpinBoxStarsIntensity->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnStarsIntensityCommitted);
	SpinBoxCloudsCoverage->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnCloudsCoverageCommitted);
	SpinBoxCloudsDensity->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnCloudsDensityCommitted);
	SpinBoxCloudsSpeed->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnCloudsSpeedCommitted);
	TerrainSurfaceCB->OnCheckStateChanged.AddDynamic(this, &UEnvironmentSectionWidget::OnTerrainSurfaceChanged);

	if (ConfigurateEnvActorsBtn)
		ConfigurateEnvActorsBtn->OnClicked.AddDynamic(this, &UEnvironmentSectionWidget::OnConfigurateEnvActorsBtnClicked);

	if (SpinBoxRainIntensity)
		SpinBoxRainIntensity->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnRainIntensityCommitted);

	if (SpinBoxStreetLightsBrightness)
		SpinBoxStreetLightsBrightness->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnStreetLightsBrightnessCommitted);

	if (ComboBoxStreetLightsDataSource)
	{
		ComboBoxStreetLightsDataSource->ClearOptions();
		ComboBoxStreetLightsDataSource->AddOption(StreetLightsDataSourceToString(EStreetLightsDataSource::Custom));
		ComboBoxStreetLightsDataSource->AddOption(StreetLightsDataSourceToString(EStreetLightsDataSource::Cesium));
	}

	LoadAndApplySavedSettings();
	SyncFromWorld();

	// Підписка — після початкового Load/Sync, щоб програмні SetSelectedOption не тригерили збереження.
	if (ComboBoxStreetLightsDataSource)
		ComboBoxStreetLightsDataSource->OnSelectionChanged.AddDynamic(this, &UEnvironmentSectionWidget::OnStreetLightsDataSourceChanged);
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

	// Значення зоряного неба не читаються назад з MID (він створюється лінькаво і може ще
	// не існувати) — панель завжди показує те, що застосував останній LoadAndApplySavedSettings
	// чи UI-хендлер, цього достатньо для узгодженості.

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

	if (SpinBoxStreetLightsBrightness)
	{
		if (AStreetLightsManager* Manager = GetStreetLightsManager())
			SpinBoxStreetLightsBrightness->SetValue(Manager->GetBrightness());
	}

	if (ComboBoxStreetLightsDataSource)
	{
		if (AStreetLightsManager* Manager = GetStreetLightsManager())
			ComboBoxStreetLightsDataSource->SetSelectedOption(StreetLightsDataSourceToString(Manager->GetDataSource()));
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
		ApplyStarsSettingsFromWidgets();
	}
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnSolarTimeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->SolarTime = (double)Value;
		SunSky->UpdateSun();
		ApplyStarsSettingsFromWidgets();
	}
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnStarsDensityCommitted(float /*Value*/, ETextCommit::Type /*CommitType*/)
{
	ApplyStarsSettingsFromWidgets();
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnStarsThresholdCommitted(float /*Value*/, ETextCommit::Type /*CommitType*/)
{
	ApplyStarsSettingsFromWidgets();
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnStarsPointSizeCommitted(float /*Value*/, ETextCommit::Type /*CommitType*/)
{
	ApplyStarsSettingsFromWidgets();
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnStarsIntensityCommitted(float /*Value*/, ETextCommit::Type /*CommitType*/)
{
	ApplyStarsSettingsFromWidgets();
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnCloudsCoverageCommitted(float /*Value*/, ETextCommit::Type /*CommitType*/)
{
	ApplyCloudsSettingsFromWidgets();
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnCloudsDensityCommitted(float /*Value*/, ETextCommit::Type /*CommitType*/)
{
	ApplyCloudsSettingsFromWidgets();
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnCloudsSpeedCommitted(float /*Value*/, ETextCommit::Type /*CommitType*/)
{
	ApplyCloudsSettingsFromWidgets();
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::ApplyCloudsSettingsFromWidgets() const
{
	AVolumetricCloud* Cloud = GetVolumetricCloud();
	if (!Cloud)
		return;

	TInlineComponentArray<UVolumetricCloudComponent*> CloudComps;
	Cloud->GetComponents(CloudComps);
	if (CloudComps.Num() == 0)
		return;

	UVolumetricCloudComponent* CloudComp = CloudComps[0];

	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CloudComp->GetMaterial());
	if (!MID)
	{
		MID = UMaterialInstanceDynamic::Create(CloudComp->GetMaterial(), CloudComp);
		CloudComp->SetMaterial(MID);
	}
	if (!MID)
		return;

	// Cloud_GlobalCoverage — скільки неба зайняте хмарами (густина хмарного покриву).
	// Cloud_GlobalDensity — оптична щільність самої хмари (менше значення = крізь хмару
	// видно більше неба, тобто вона прозоріша).
	MID->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), (float)SpinBoxCloudsCoverage->GetValue());
	MID->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), (float)SpinBoxCloudsDensity->GetValue());

	// Layout_WindControls = (напрям.X, напрям.Y, швидкість, швидкість деталізації) —
	// напрям лишаємо як у дефолтному матеріалі (1,1), масштабуємо лише швидкість за
	// тим самим співвідношенням, що й дефолт (0.5 / 0.333 ≈ 0.666).
	const float WindSpeed = (float)SpinBoxCloudsSpeed->GetValue();
	MID->SetVectorParameterValue(TEXT("Layout_WindControls"), FLinearColor(1.0f, 1.0f, 0.5f * WindSpeed, 0.333f * WindSpeed));
}

void UEnvironmentSectionWidget::ApplyStarsSettingsFromWidgets() const
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		UpdateNightVisuals(SunSky,
			(float)SpinBoxStarsDensity->GetValue(),
			(float)SpinBoxStarsThreshold->GetValue(),
			(float)SpinBoxStarsPointSize->GetValue(),
			(float)SpinBoxStarsIntensity->GetValue());
	}
}

void UEnvironmentSectionWidget::UpdateNightVisuals(ACesiumSunSky* SunSky, float StarsDensity, float StarsThreshold, float StarsPointSize, float StarsIntensity) const
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
			MID->SetScalarParameterValue(TEXT("StarsDensity"), StarsDensity);
			MID->SetScalarParameterValue(TEXT("StarsThreshold"), StarsThreshold);
			MID->SetScalarParameterValue(TEXT("StarsPointSize"), StarsPointSize);
			MID->SetScalarParameterValue(TEXT("StarsIntensity"), StarsIntensity);
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

void UEnvironmentSectionWidget::OnStreetLightsBrightnessCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (AStreetLightsManager* Manager = GetStreetLightsManager())
		Manager->SetBrightness(Value);
	SaveCurrentSettings();
}

void UEnvironmentSectionWidget::OnStreetLightsDataSourceChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
	if (AStreetLightsManager* Manager = GetStreetLightsManager())
		Manager->SetDataSource(StringToStreetLightsDataSource(SelectedItem));
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

	SpinBoxStarsDensity->SetValue((float)Save->StarsDensity);
	SpinBoxStarsThreshold->SetValue((float)Save->StarsThreshold);
	SpinBoxStarsPointSize->SetValue((float)Save->StarsPointSize);
	SpinBoxStarsIntensity->SetValue((float)Save->StarsIntensity);

	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->TimeZone  = Save->TimeZone;
		SunSky->SolarTime = Save->SolarTime;
		SunSky->UpdateSun();
		UpdateNightVisuals(SunSky, (float)Save->StarsDensity, (float)Save->StarsThreshold, (float)Save->StarsPointSize, (float)Save->StarsIntensity);
	}

	SpinBoxCloudsCoverage->SetValue((float)Save->CloudsCoverage);
	SpinBoxCloudsDensity->SetValue((float)Save->CloudsDensity);
	SpinBoxCloudsSpeed->SetValue((float)Save->CloudsSpeed);
	ApplyCloudsSettingsFromWidgets();

	ApplyTerrainSurfaceState(Save->bTerrainSurfaceEnabled);

	if (ARainEffectManager* Manager = GetRainEffectManager())
	{
		Manager->SetRainIntensity((float)Save->RainIntensity);
	}

	if (SpinBoxStreetLightsBrightness)
		SpinBoxStreetLightsBrightness->SetValue((float)Save->StreetLightsBrightness);

	if (AStreetLightsManager* Manager = GetStreetLightsManager())
	{
		Manager->SetBrightness((float)Save->StreetLightsBrightness);
		Manager->SetDataSource(Save->StreetLightsDataSource);
	}

	if (ComboBoxStreetLightsDataSource)
		ComboBoxStreetLightsDataSource->SetSelectedOption(StreetLightsDataSourceToString(Save->StreetLightsDataSource));
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

	Save->StarsDensity   = SpinBoxStarsDensity->GetValue();
	Save->StarsThreshold = SpinBoxStarsThreshold->GetValue();
	Save->StarsPointSize = SpinBoxStarsPointSize->GetValue();
	Save->StarsIntensity = SpinBoxStarsIntensity->GetValue();

	Save->CloudsCoverage = SpinBoxCloudsCoverage->GetValue();
	Save->CloudsDensity  = SpinBoxCloudsDensity->GetValue();
	Save->CloudsSpeed    = SpinBoxCloudsSpeed->GetValue();

	if (ACesium3DTileset* Tileset = GetTileset())
	{
		Save->bTerrainSurfaceEnabled = !Tileset->IsHidden();
	}

	if (ARainEffectManager* Manager = GetRainEffectManager())
	{
		Save->RainIntensity = Manager->GetRainIntensity();
	}

	if (AStreetLightsManager* Manager = GetStreetLightsManager())
	{
		Save->StreetLightsBrightness = Manager->GetBrightness();
		Save->StreetLightsDataSource = Manager->GetDataSource();
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

AVolumetricCloud* UEnvironmentSectionWidget::GetVolumetricCloud() const
{
	if (UWorld* World = GetWorld())
		return Cast<AVolumetricCloud>(UGameplayStatics::GetActorOfClass(World, AVolumetricCloud::StaticClass()));
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

AStreetLightsManager* UEnvironmentSectionWidget::GetStreetLightsManager() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	if (AStreetLightsManager* Existing = Cast<AStreetLightsManager>(
			UGameplayStatics::GetActorOfClass(World, AStreetLightsManager::StaticClass())))
	{
		return Existing;
	}

	// У рівні ще немає розміщеного менеджера — спінбоксу нема кого перемикати. StreetLightsSystem
	// лишиться незаданим, доки його не признать вручну в редакторі (як RainSystem вище).
	AStreetLightsManager* Spawned = World->SpawnActor<AStreetLightsManager>();
	UE_LOG(LogUAV, Log, TEXT("EnvironmentSectionWidget::GetStreetLightsManager: no AStreetLightsManager in level — spawned %s"),
		Spawned ? *Spawned->GetName() : TEXT("FAILED"));
	return Spawned;
}
