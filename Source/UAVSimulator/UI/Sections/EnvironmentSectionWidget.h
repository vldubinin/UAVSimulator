#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "EnvironmentSectionWidget.generated.h"

class USpinBox;
class UCheckBox;
class ACesiumGeoreference;
class ACesiumSunSky;
class ACesium3DTileset;
class AUAVSimulatorGameModeBase;
class AEWZoneActor;

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

	// — EW (Electronic Warfare) перешкоди ——————————————————————————————————————
	// OptionalWidget = true: якщо в UMG Blueprint ще не додано (або неточно названо) один
	// із цих віджетів, секція не повинна падати на BindWidget і ламати решту (Cesium тощо) —
	// кожне використання нижче захищене перевіркою на null, як CesiumSurroundingsCB в
	// SensorsSectionWidget.

	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UCheckBox> IsEnabledEWCB;

	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<USpinBox> SpinBoxEWLocationX;

	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<USpinBox> SpinBoxEWLocationY;

	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<USpinBox> SpinBoxEWRadius;

	// — Резервні небо/сонце, що показуються, поки ландшафт Cesium вимкнено ————————

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Environment|DefaultSky")
	TSubclassOf<AActor> DefaultSkyboxClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Environment|DefaultSky")
	TSubclassOf<AActor> DefaultSunClass;

private:
	void SyncFromWorld();
	void ApplyTerrainSurfaceState(bool bEnabled);
	void ApplyEWZoneVisualState(bool bEnabled);
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	ACesiumGeoreference* GetGeoreference() const;
	ACesiumSunSky*       GetSunSky() const;
	ACesium3DTileset*    GetTileset() const;
	AUAVSimulatorGameModeBase* GetGameMode() const;
	AEWZoneActor*        GetEWZone() const;

	static const FString EnvironmentSaveSlotName;

	UFUNCTION() void OnOriginLatitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginLongitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginHeightCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTimeZoneCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnSolarTimeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTerrainSurfaceChanged(bool bIsChecked);
	UFUNCTION() void OnEWEnabledChanged(bool bIsChecked);
	UFUNCTION() void OnEWLocationXCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnEWLocationYCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnEWRadiusCommitted(float Value, ETextCommit::Type CommitType);

	/** OnValueChanged (а не лише Committed) — щоб сфера рухалась одразу під час
	 *  перетягування повзунка, ще до Enter/втрати фокуса й ще до Start Simulation. */
	UFUNCTION() void OnEWLocationXChanged(float Value);
	UFUNCTION() void OnEWLocationYChanged(float Value);
	UFUNCTION() void OnEWRadiusChanged(float Value);

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedDefaultSkybox;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedDefaultSun;
};
