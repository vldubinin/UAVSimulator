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
	TObjectPtr<UCheckBox> AltimeterCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> CameraInclinationCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> LidarCB;

	virtual void OnSectionActivated_Implementation() override;

private:
	void SyncFromGameMode();

	AUAVSimulatorGameModeBase* GetGameMode() const;

	UFUNCTION() void OnCameraFrameChanged(bool bIsChecked);
	UFUNCTION() void OnAltimeterChanged(bool bIsChecked);
	UFUNCTION() void OnCameraInclinationChanged(bool bIsChecked);
	UFUNCTION() void OnLidarChanged(bool bIsChecked);
};
