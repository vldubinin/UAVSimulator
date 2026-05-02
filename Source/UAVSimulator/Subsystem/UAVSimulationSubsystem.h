// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UAVSimulationSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnVisualSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnCameraSettingsChanged);

UCLASS()
class UAVSIMULATOR_API UUAVSimulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool bEnableVisualsForPlayer = true;
	bool bEnableVisualsForTarget = false;
	bool bEnableCameraForPlayer = true;
	bool bEnableCameraForTarget = false;

	FOnVisualSettingsChanged OnVisualSettingsChanged;
	FOnCameraSettingsChanged OnCameraSettingsChanged;

	void SetVisualSettings(bool bInPlayer, bool bInTarget);
	void SetCameraSettings(bool bInPlayer, bool bInTarget);
};
