// Fill out your copyright notice in the Description page of Project Settings.

#include "UAVSimulationSubsystem.h"

void UUAVSimulationSubsystem::SetVisualSettings(bool bInPlayer, bool bInTarget)
{
	bEnableVisualsForPlayer = bInPlayer;
	bEnableVisualsForTarget = bInTarget;
	OnVisualSettingsChanged.Broadcast();
}

void UUAVSimulationSubsystem::SetCameraSettings(bool bInPlayer, bool bInTarget)
{
	bEnableCameraForPlayer = bInPlayer;
	bEnableCameraForTarget = bInTarget;
	OnCameraSettingsChanged.Broadcast();
}

void UUAVSimulationSubsystem::SetSensorSettings(bool bAltimeter, bool bCameraInclination, bool bLidar, bool bCameraFrame, bool bCameraAltitude, bool bSegmentationMask, bool bBBoxDetection, bool bPosition)
{
	bEnableSensorAltimeter         = bAltimeter;
	bEnableSensorCameraInclination = bCameraInclination;
	bEnableSensorLidar             = bLidar;
	bEnableSensorCameraFrame	   = bCameraFrame;
	bEnableSensorCameraAltitude    = bCameraAltitude;
	bEnableSensorSegmentationMask  = bSegmentationMask;
	bEnableSensorBBoxDetection     = bBBoxDetection;
	bEnableSensorPosition          = bPosition;
	OnSensorSettingsChanged.Broadcast();
}
