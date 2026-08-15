#include "GlobalSectionWidget.h"
#include "UAVSimulator/UAVSimulatorGameModeBase.h"
#include "Components/SpinBox.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Save/GlobalSettingsSave.h"

const FString UGlobalSectionWidget::GlobalSaveSlotName = TEXT("GlobalSettings");

void UGlobalSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	LoadAndApplySavedSettings();
	SyncFromGameMode();

	SpinBoxSensorWarmupFrames->OnValueChanged.AddDynamic(this, &UGlobalSectionWidget::OnSensorWarmupFramesChanged);
}

void UGlobalSectionWidget::OnSectionActivated_Implementation()
{
	SyncFromGameMode();
}

void UGlobalSectionWidget::SyncFromGameMode()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM) return;

	SpinBoxSensorWarmupFrames->SetValue(static_cast<float>(GM->SensorWarmupFrameCount));
}

void UGlobalSectionWidget::OnSensorWarmupFramesChanged(float Value)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->SensorWarmupFrameCount = FMath::RoundToInt(Value);
	SaveCurrentSettings();
}

void UGlobalSectionWidget::LoadAndApplySavedSettings()
{
	UGlobalSettingsSave* Save = Cast<UGlobalSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(GlobalSaveSlotName, /*UserIndex=*/0));
	if (!Save)
		return;

	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM)
		return;

	GM->SensorWarmupFrameCount = Save->SensorWarmupFrameCount;
}

void UGlobalSectionWidget::SaveCurrentSettings()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM)
		return;

	UGlobalSettingsSave* Save = Cast<UGlobalSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UGlobalSettingsSave::StaticClass()));

	Save->SensorWarmupFrameCount = GM->SensorWarmupFrameCount;

	UGameplayStatics::SaveGameToSlot(Save, GlobalSaveSlotName, /*UserIndex=*/0);
}

AUAVSimulatorGameModeBase* UGlobalSectionWidget::GetGameMode() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<AUAVSimulatorGameModeBase>(World->GetAuthGameMode());
	}
	return nullptr;
}
