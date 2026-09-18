#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "GlobalSectionWidget.generated.h"

class USpinBox;
class UCheckBox;
class AUAVSimulatorGameModeBase;
class UUAVSimulationSubsystem;

/**
 * Глобальні налаштування симулятора, незалежні від конкретного розділу сенсорів/сценарію/середовища.
 * Містить SensorWarmupFrameCount (кількість кадрів "прогріву" сенсорів перед публікацією — сама логіка
 * прогріву ще не реалізована, цей розділ лише надає значення для налаштування й збереження) та параметри
 * експозиції бортової камери (ISO/витримка/діафрагма/EV-компенсація/Manual чи Auto), щоб її можна було
 * підлаштувати під характеристики реального сенсора-прототипу.
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

	// — Експозиція бортової камери ————————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> CheckBoxCameraManualExposure;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxCameraISO;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxCameraShutterSpeed;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxCameraApertureFStop;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxCameraExposureBias;

private:
	void SyncFromGameMode();
	void SyncCameraExposureFromSubsystem();
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();
	void ApplyCameraExposureSettings();

	AUAVSimulatorGameModeBase* GetGameMode() const;
	UUAVSimulationSubsystem*   GetSimulationSubsystem() const;

	UFUNCTION() void OnSensorWarmupFramesChanged(float Value);
	UFUNCTION() void OnCameraManualExposureChanged(bool bIsChecked);
	UFUNCTION() void OnCameraISOChanged(float Value);
	UFUNCTION() void OnCameraShutterSpeedChanged(float Value);
	UFUNCTION() void OnCameraApertureFStopChanged(float Value);
	UFUNCTION() void OnCameraExposureBiasChanged(float Value);

	static const FString GlobalSaveSlotName;
};
