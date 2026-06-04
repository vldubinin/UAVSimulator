#include "SensorsSectionWidget.h"
#include "UAVSimulator/UAVSimulatorGameModeBase.h"
#include "Components/CheckBox.h"
#include "Kismet/GameplayStatics.h"

void USensorsSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SyncFromGameMode();

	CameraFrameCB->OnCheckStateChanged.AddDynamic(this, &USensorsSectionWidget::OnCameraFrameChanged);
	AltimeterCB->OnCheckStateChanged.AddDynamic(this, &USensorsSectionWidget::OnAltimeterChanged);
	CameraInclinationCB->OnCheckStateChanged.AddDynamic(this, &USensorsSectionWidget::OnCameraInclinationChanged);
	LidarCB->OnCheckStateChanged.AddDynamic(this, &USensorsSectionWidget::OnLidarChanged);
}

void USensorsSectionWidget::OnSectionActivated_Implementation()
{
	SyncFromGameMode();
}

void USensorsSectionWidget::SyncFromGameMode()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM) return;

	CameraFrameCB->SetIsChecked(GM->bEnableCameraForPlayer);
	AltimeterCB->SetIsChecked(GM->bEnableSensorAltimeter);
	CameraInclinationCB->SetIsChecked(GM->bEnableSensorCameraInclination);
	LidarCB->SetIsChecked(GM->bEnableSensorLidar);
}

void USensorsSectionWidget::OnCameraFrameChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		GM->bEnableCameraForPlayer = bIsChecked;
		GM->UpdateCameraSettings();
	}
}

void USensorsSectionWidget::OnAltimeterChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		GM->bEnableSensorAltimeter = bIsChecked;
		GM->UpdateSensorSettings();
	}
}

void USensorsSectionWidget::OnCameraInclinationChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		GM->bEnableSensorCameraInclination = bIsChecked;
		GM->UpdateSensorSettings();
	}
}

void USensorsSectionWidget::OnLidarChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		GM->bEnableSensorLidar = bIsChecked;
		GM->UpdateSensorSettings();
	}
}

AUAVSimulatorGameModeBase* USensorsSectionWidget::GetGameMode() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<AUAVSimulatorGameModeBase>(World->GetAuthGameMode());
	}
	return nullptr;
}
