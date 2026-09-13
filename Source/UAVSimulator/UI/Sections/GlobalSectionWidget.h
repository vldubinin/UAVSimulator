#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "GlobalSectionWidget.generated.h"

class USpinBox;
class AUAVSimulatorGameModeBase;

/**
 * Глобальні налаштування симулятора, незалежні від конкретного розділу сенсорів/сценарію/середовища.
 * Наразі містить лише SensorWarmupFrameCount (кількість кадрів "прогріву" сенсорів перед публікацією) —
 * сама логіка прогріву ще не реалізована, цей розділ лише надає значення для налаштування й збереження,
 * так само як інші розділи роблять для власних полів.
 */
UCLASS()
class UAVSIMULATOR_API UGlobalSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void OnSectionActivated_Implementation() override;

	// — Прив'язані віджети (ім'я має точно збігатися з Blueprint) ————————————————

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
