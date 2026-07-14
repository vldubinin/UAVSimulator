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

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> CameraInclinationCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> LidarCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> PositionCB;

	/**
	 * Optional — meta=(BindWidget) alone (without "optional") would normally require this
	 * widget to exist in the paired UMG Blueprint at construction time. Until someone adds a
	 * "CesiumSurroundingsCB" checkbox to that Blueprint in the UMG editor, this stays null;
	 * every use below is guarded accordingly instead of assuming it's bound like the others.
	 */
	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UCheckBox> CesiumSurroundingsCB;

	virtual void OnSectionActivated_Implementation() override;

private:
	void SyncFromGameMode();
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	AUAVSimulatorGameModeBase* GetGameMode() const;

	UFUNCTION() void OnCameraFrameChanged(bool bIsChecked);
	UFUNCTION() void OnAltimeterChanged(bool bIsChecked);
	UFUNCTION() void OnCameraInclinationChanged(bool bIsChecked);
	UFUNCTION() void OnLidarChanged(bool bIsChecked);
	UFUNCTION() void OnCameraAltitude(bool bIsChecked);
	UFUNCTION() void OnPositionChanged(bool bIsChecked);
	UFUNCTION() void OnCesiumSurroundingsChanged(bool bIsChecked);

	static const FString SensorSaveSlotName;
};
