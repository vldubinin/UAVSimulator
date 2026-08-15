#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "GlobalSectionWidget.generated.h"

class USpinBox;
class AUAVSimulatorGameModeBase;

/**
 * Global simulator configuration, independent of any single sensor/scenario/environment section.
 * Currently holds only SensorWarmupFrameCount (the number of frames sensors should warm up for
 * before publishing) — the warm-up logic itself is not implemented yet, this section just exposes
 * the value for configuration and persistence, same as the other sections do for their own fields.
 */
UCLASS()
class UAVSIMULATOR_API UGlobalSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void OnSectionActivated_Implementation() override;

	// — Bound widgets (name must match exactly in the Blueprint) ————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxSensorWarmupFrames;

private:
	void SyncFromGameMode();
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	AUAVSimulatorGameModeBase* GetGameMode() const;

	UFUNCTION() void OnSensorWarmupFramesChanged(float Value);

	static const FString GlobalSaveSlotName;
};
