#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "EnvironmentSectionWidget.generated.h"

class USpinBox;
class UCheckBox;
class UButton;
class ACesiumGeoreference;
class ACesiumSunSky;
class ACesium3DTileset;
class AEnvironmentActorManager;

UCLASS()
class UAVSIMULATOR_API UEnvironmentSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void OnSectionActivated_Implementation() override;

	// — CesiumGeoreference (початок координат) ——————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginLatitude;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginLongitude;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginHeight;

	// — CesiumSunSky (сонце/небо) ————————————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxTimeZone;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxSolarTime;

	// — Cesium3DTileset (тайли ландшафту) ————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> TerrainSurfaceCB;

	// — Налаштування наземних об'єктів (map-marker) ————————————————————————————
	// Позиція/радіус зон РЕБ налаштовуються через ConfigurateEnvActorsBtn (карта) і живуть
	// в AEnvironmentActorManager::EWConfigurations.
	// OptionalWidget = true: те саме застереження, що й для EW-віджетів вище — кнопки
	// ще нема в UMG Blueprint, доки її не додадуть вручну.

	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UButton> ConfigurateEnvActorsBtn;

	// — Резервні небо/сонце, що показуються, поки ландшафт Cesium вимкнено ————————

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Environment|DefaultSky")
	TSubclassOf<AActor> DefaultSkyboxClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Environment|DefaultSky")
	TSubclassOf<AActor> DefaultSunClass;

private:
	void SyncFromWorld();
	void ApplyTerrainSurfaceState(bool bEnabled);
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	ACesiumGeoreference* GetGeoreference() const;
	ACesiumSunSky*       GetSunSky() const;
	ACesium3DTileset*    GetTileset() const;
	AEnvironmentActorManager* GetEnvironmentActorManager() const;

	static const FString EnvironmentSaveSlotName;

	UFUNCTION() void OnOriginLatitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginLongitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginHeightCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTimeZoneCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnSolarTimeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTerrainSurfaceChanged(bool bIsChecked);

	UFUNCTION() void OnConfigurateEnvActorsBtnClicked();

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedDefaultSkybox;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedDefaultSun;
};
