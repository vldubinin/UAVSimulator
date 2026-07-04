// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UAVSimulator/Entity/SimulatorMode.h"
#include "UAVSimulationSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnVisualSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnCameraSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnSensorSettingsChanged);

UCLASS()
class UAVSIMULATOR_API UUAVSimulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	ESimulatorMode CurrentSimulatorMode = ESimulatorMode::RecordTarget;

	bool bEnableVisualsForPlayer = true;
	bool bEnableVisualsForTarget = false;
	bool bEnableCameraForPlayer = true;
	bool bEnableCameraForTarget = false;

	bool bEnableSensorAltimeter          = true;
	bool bEnableSensorCameraInclination  = true;
	bool bEnableSensorLidar              = true;
	bool bEnableSensorCameraFrame        = true;
	bool bEnableSensorCameraAltitude     = true;
	bool bEnableSensorSegmentationMask   = true;
	bool bEnableSensorBBoxDetection      = true;
	bool bEnableSensorPosition           = true;

	FOnVisualSettingsChanged OnVisualSettingsChanged;
	FOnCameraSettingsChanged OnCameraSettingsChanged;
	FOnSensorSettingsChanged OnSensorSettingsChanged;

	void SetVisualSettings(bool bInPlayer, bool bInTarget);
	void SetCameraSettings(bool bInPlayer, bool bInTarget);
	void SetSensorSettings(bool bAltimeter, bool bCameraInclination, bool bLidar, bool bCameraFrame, bool bCameraAltitude, bool bSegmentationMask, bool bBBoxDetection, bool bPosition);
};
