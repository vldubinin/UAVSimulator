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
	CameraAltitudeCB->OnCheckStateChanged.AddDynamic(this, &USensorsSectionWidget::OnCameraAltitude);
	PositionCB->OnCheckStateChanged.AddDynamic(this, &USensorsSectionWidget::OnPositionChanged);
}


void USensorsSectionWidget::OnSectionActivated_Implementation()
{
	SyncFromGameMode();
}

void USensorsSectionWidget::SyncFromGameMode()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM) return;

	CameraFrameCB->SetIsChecked(GM->bEnableSensorCameraFrame);
	AltimeterCB->SetIsChecked(GM->bEnableSensorAltimeter);
	CameraInclinationCB->SetIsChecked(GM->bEnableSensorCameraInclination);
	LidarCB->SetIsChecked(GM->bEnableSensorLidar);
	CameraAltitudeCB->SetIsChecked(GM->bEnableSensorCameraAltitude);
	PositionCB->SetIsChecked(GM->bEnableSensorPosition);
}

void USensorsSectionWidget::OnCameraFrameChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorCameraFrame = bIsChecked;
}

void USensorsSectionWidget::OnAltimeterChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorAltimeter = bIsChecked;
}

void USensorsSectionWidget::OnCameraInclinationChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorCameraInclination = bIsChecked;
}

void USensorsSectionWidget::OnLidarChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorLidar = bIsChecked;
}

void USensorsSectionWidget::OnCameraAltitude(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorCameraAltitude = bIsChecked;
}

void USensorsSectionWidget::OnPositionChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorPosition = bIsChecked;
}

AUAVSimulatorGameModeBase* USensorsSectionWidget::GetGameMode() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<AUAVSimulatorGameModeBase>(World->GetAuthGameMode());
	}
	return nullptr;
}
