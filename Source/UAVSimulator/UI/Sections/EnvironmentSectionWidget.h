#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "EnvironmentSectionWidget.generated.h"

class USpinBox;
class UCheckBox;
class ACesiumGeoreference;
class ACesiumSunSky;
class ACesium3DTileset;

UCLASS()
class UAVSIMULATOR_API UEnvironmentSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void OnSectionActivated_Implementation() override;

	// — CesiumGeoreference ————————————————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginLatitude;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginLongitude;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginHeight;

	// — CesiumSunSky ——————————————————————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxTimeZone;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxSolarTime;

	// — Cesium3DTileset ——————————————————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> TerrainSurfaceCB;

	// — Fallback sky/sun shown while the Cesium terrain is disabled ————————————

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

	static const FString EnvironmentSaveSlotName;

	UFUNCTION() void OnOriginLatitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginLongitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginHeightCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTimeZoneCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnSolarTimeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTerrainSurfaceChanged(bool bIsChecked);

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedDefaultSkybox;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedDefaultSun;
};
