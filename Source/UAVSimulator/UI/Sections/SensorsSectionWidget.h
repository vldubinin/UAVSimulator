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

	virtual void OnSectionActivated_Implementation() override;

private:
	void SyncFromGameMode();

	AUAVSimulatorGameModeBase* GetGameMode() const;

	UFUNCTION() void OnCameraFrameChanged(bool bIsChecked);
	UFUNCTION() void OnAltimeterChanged(bool bIsChecked);
	UFUNCTION() void OnCameraInclinationChanged(bool bIsChecked);
	UFUNCTION() void OnLidarChanged(bool bIsChecked);
	UFUNCTION() void OnCameraAltitude(bool bIsChecked);
	UFUNCTION() void OnPositionChanged(bool bIsChecked);
};
