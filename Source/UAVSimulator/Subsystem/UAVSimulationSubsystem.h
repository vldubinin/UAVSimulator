// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UAVSimulationSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnVisualSettingsChanged);

UCLASS()
class UAVSIMULATOR_API UUAVSimulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool bEnableVisualsForPlayer = true;
	bool bEnableVisualsForTarget = false;

	FOnVisualSettingsChanged OnVisualSettingsChanged;

	void SetVisualSettings(bool bInPlayer, bool bInTarget);
};
