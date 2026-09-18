#include "GlobalSectionWidget.h"
#include "UAVSimulator/UAVSimulatorGameModeBase.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Save/GlobalSettingsSave.h"

const FString UGlobalSectionWidget::GlobalSaveSlotName = TEXT("GlobalSettings");

void UGlobalSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	LoadAndApplySavedSettings();
	SyncFromGameMode();
	SyncCameraExposureFromSubsystem();

	SpinBoxSensorWarmupFrames->OnValueChanged.AddDynamic(this, &UGlobalSectionWidget::OnSensorWarmupFramesChanged);
	CheckBoxCameraManualExposure->OnCheckStateChanged.AddDynamic(this, &UGlobalSectionWidget::OnCameraManualExposureChanged);
	SpinBoxCameraISO->OnValueChanged.AddDynamic(this, &UGlobalSectionWidget::OnCameraISOChanged);
	SpinBoxCameraShutterSpeed->OnValueChanged.AddDynamic(this, &UGlobalSectionWidget::OnCameraShutterSpeedChanged);
	SpinBoxCameraApertureFStop->OnValueChanged.AddDynamic(this, &UGlobalSectionWidget::OnCameraApertureFStopChanged);
	SpinBoxCameraExposureBias->OnValueChanged.AddDynamic(this, &UGlobalSectionWidget::OnCameraExposureBiasChanged);
}

void UGlobalSectionWidget::OnSectionActivated_Implementation()
{
	SyncFromGameMode();
	SyncCameraExposureFromSubsystem();
}

void UGlobalSectionWidget::SyncFromGameMode()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM) return;

	SpinBoxSensorWarmupFrames->SetValue(static_cast<float>(GM->SensorWarmupFrameCount));
}

void UGlobalSectionWidget::SyncCameraExposureFromSubsystem()
{
	UUAVSimulationSubsystem* Subsystem = GetSimulationSubsystem();
	if (!Subsystem) return;

	CheckBoxCameraManualExposure->SetIsChecked(Subsystem->bCameraManualExposure);
	SpinBoxCameraISO->SetValue(Subsystem->CameraISO);
	SpinBoxCameraShutterSpeed->SetValue(Subsystem->CameraShutterSpeed);
	SpinBoxCameraApertureFStop->SetValue(Subsystem->CameraApertureFStop);
	SpinBoxCameraExposureBias->SetValue(Subsystem->CameraExposureBias);
}

void UGlobalSectionWidget::OnSensorWarmupFramesChanged(float Value)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->SensorWarmupFrameCount = FMath::RoundToInt(Value);
	SaveCurrentSettings();
}

void UGlobalSectionWidget::OnCameraManualExposureChanged(bool /*bIsChecked*/)
{
	ApplyCameraExposureSettings();
}

void UGlobalSectionWidget::OnCameraISOChanged(float /*Value*/)
{
	ApplyCameraExposureSettings();
}

void UGlobalSectionWidget::OnCameraShutterSpeedChanged(float /*Value*/)
{
	ApplyCameraExposureSettings();
}

void UGlobalSectionWidget::OnCameraApertureFStopChanged(float /*Value*/)
{
	ApplyCameraExposureSettings();
}

void UGlobalSectionWidget::OnCameraExposureBiasChanged(float /*Value*/)
{
	ApplyCameraExposureSettings();
}

void UGlobalSectionWidget::ApplyCameraExposureSettings()
{
	UUAVSimulationSubsystem* Subsystem = GetSimulationSubsystem();
	if (!Subsystem) return;

	Subsystem->SetCameraExposureSettings(
		CheckBoxCameraManualExposure->IsChecked(),
		static_cast<float>(SpinBoxCameraISO->GetValue()),
		static_cast<float>(SpinBoxCameraShutterSpeed->GetValue()),
		static_cast<float>(SpinBoxCameraApertureFStop->GetValue()),
		static_cast<float>(SpinBoxCameraExposureBias->GetValue()));

	SaveCurrentSettings();
}

void UGlobalSectionWidget::LoadAndApplySavedSettings()
{
	UGlobalSettingsSave* Save = Cast<UGlobalSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(GlobalSaveSlotName, /*UserIndex=*/0));
	if (!Save)
		return;

	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->SensorWarmupFrameCount = Save->SensorWarmupFrameCount;

	if (UUAVSimulationSubsystem* Subsystem = GetSimulationSubsystem())
	{
		Subsystem->SetCameraExposureSettings(
			Save->bCameraManualExposure,
			Save->CameraISO,
			Save->CameraShutterSpeed,
			Save->CameraApertureFStop,
			Save->CameraExposureBias);
	}
}

void UGlobalSectionWidget::SaveCurrentSettings()
{
	UGlobalSettingsSave* Save = Cast<UGlobalSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UGlobalSettingsSave::StaticClass()));

	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		Save->SensorWarmupFrameCount = GM->SensorWarmupFrameCount;

	if (UUAVSimulationSubsystem* Subsystem = GetSimulationSubsystem())
	{
		Save->bCameraManualExposure = Subsystem->bCameraManualExposure;
		Save->CameraISO             = Subsystem->CameraISO;
		Save->CameraShutterSpeed    = Subsystem->CameraShutterSpeed;
		Save->CameraApertureFStop   = Subsystem->CameraApertureFStop;
		Save->CameraExposureBias    = Subsystem->CameraExposureBias;
	}

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

UUAVSimulationSubsystem* UGlobalSectionWidget::GetSimulationSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		return World->GetSubsystem<UUAVSimulationSubsystem>();
	}
	return nullptr;
}
