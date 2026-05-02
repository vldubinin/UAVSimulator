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
