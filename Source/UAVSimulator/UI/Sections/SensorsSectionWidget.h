#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "SensorsSectionWidget.generated.h"

class UCheckBox;
class AUAVSimulatorGameModeBase;

UCLASS()
class UAVSIMULATOR_API USensorsSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> CameraFrameCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> CameraAltitudeCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> AltimeterCB;

	/** Опціонально — за тим самим принципом, що й CesiumSurroundingsCB; щоб прив'язати, додайте чекбокс "AttitudeIndicatorCB" до UMG Blueprint. */
	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UCheckBox> AttitudeIndicatorCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> CameraInclinationCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> LidarCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> PositionCB;

	/** Опціонально — за тим самим принципом, що й CesiumSurroundingsCB; щоб прив'язати, додайте чекбокс "GeoPositionCB" до UMG Blueprint. */
	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UCheckBox> GeoPositionCB;

	/**
	 * Опціонально — саме лише meta=(BindWidget) (без "optional") зазвичай вимагало б, щоб цей
	 * віджет існував у парному UMG Blueprint на момент конструювання. Поки хтось не додасть
	 * чекбокс "CesiumSurroundingsCB" до цього Blueprint у редакторі UMG, тут залишається null;
	 * тому кожне використання нижче відповідно захищене перевіркою, а не вважається прив'язаним
	 * без умов, як інші поля.
	 */
	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UCheckBox> CesiumSurroundingsCB;

	/** Опціонально — за тим самим принципом, що й CesiumSurroundingsCB; щоб прив'язати, додайте чекбокс "CustomSurroundingsCB" до UMG Blueprint. */
	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UCheckBox> CustomSurroundingsCB;

	virtual void OnSectionActivated_Implementation() override;

private:
	void SyncFromGameMode();
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	AUAVSimulatorGameModeBase* GetGameMode() const;

	UFUNCTION() void OnCameraFrameChanged(bool bIsChecked);
	UFUNCTION() void OnAltimeterChanged(bool bIsChecked);
	UFUNCTION() void OnAttitudeIndicatorChanged(bool bIsChecked);
	UFUNCTION() void OnCameraInclinationChanged(bool bIsChecked);
	UFUNCTION() void OnLidarChanged(bool bIsChecked);
	UFUNCTION() void OnCameraAltitude(bool bIsChecked);
	UFUNCTION() void OnPositionChanged(bool bIsChecked);
	UFUNCTION() void OnGeoPositionChanged(bool bIsChecked);
	UFUNCTION() void OnCesiumSurroundingsChanged(bool bIsChecked);
	UFUNCTION() void OnCustomSurroundingsChanged(bool bIsChecked);

	static const FString SensorSaveSlotName;
};
