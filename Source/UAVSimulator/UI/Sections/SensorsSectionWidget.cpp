#include "SensorsSectionWidget.h"
#include "UAVSimulator/UAVSimulatorGameModeBase.h"
#include "Components/CheckBox.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Save/SensorSettingsSave.h"

const FString USensorsSectionWidget::SensorSaveSlotName = TEXT("SensorSettings");

void USensorsSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	LoadAndApplySavedSettings();
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
	SaveCurrentSettings();
}

void USensorsSectionWidget::OnAltimeterChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorAltimeter = bIsChecked;
	SaveCurrentSettings();
}

void USensorsSectionWidget::OnCameraInclinationChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorCameraInclination = bIsChecked;
	SaveCurrentSettings();
}

void USensorsSectionWidget::OnLidarChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorLidar = bIsChecked;
	SaveCurrentSettings();
}

void USensorsSectionWidget::OnCameraAltitude(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorCameraAltitude = bIsChecked;
	SaveCurrentSettings();
}

void USensorsSectionWidget::OnPositionChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorPosition = bIsChecked;
	SaveCurrentSettings();
}

void USensorsSectionWidget::LoadAndApplySavedSettings()
{
	USensorSettingsSave* Save = Cast<USensorSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(SensorSaveSlotName, /*UserIndex=*/0));
	if (!Save)
		return;

	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM)
		return;

	GM->bEnableSensorCameraFrame       = Save->bEnableSensorCameraFrame;
	GM->bEnableSensorAltimeter         = Save->bEnableSensorAltimeter;
	GM->bEnableSensorCameraInclination = Save->bEnableSensorCameraInclination;
	GM->bEnableSensorLidar             = Save->bEnableSensorLidar;
	GM->bEnableSensorCameraAltitude    = Save->bEnableSensorCameraAltitude;
	GM->bEnableSensorPosition          = Save->bEnableSensorPosition;
}

void USensorsSectionWidget::SaveCurrentSettings()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM)
		return;

	USensorSettingsSave* Save = Cast<USensorSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(USensorSettingsSave::StaticClass()));

	Save->bEnableSensorCameraFrame       = GM->bEnableSensorCameraFrame;
	Save->bEnableSensorAltimeter         = GM->bEnableSensorAltimeter;
	Save->bEnableSensorCameraInclination = GM->bEnableSensorCameraInclination;
	Save->bEnableSensorLidar             = GM->bEnableSensorLidar;
	Save->bEnableSensorCameraAltitude    = GM->bEnableSensorCameraAltitude;
	Save->bEnableSensorPosition          = GM->bEnableSensorPosition;

	UGameplayStatics::SaveGameToSlot(Save, SensorSaveSlotName, /*UserIndex=*/0);
}

AUAVSimulatorGameModeBase* USensorsSectionWidget::GetGameMode() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<AUAVSimulatorGameModeBase>(World->GetAuthGameMode());
	}
	return nullptr;
}
