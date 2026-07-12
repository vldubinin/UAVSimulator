// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "UAVSimulator/Entity/SimulatorMode.h"

#include "ScenarioSettingsSave.generated.h"

UCLASS()
class UAVSIMULATOR_API UScenarioSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite)
	ESimulatorMode CurrentSimulatorMode = ESimulatorMode::RecordTarget;

	UPROPERTY(BlueprintReadWrite)
	FString ScenarioSlotName;

	UPROPERTY(BlueprintReadWrite)
	float TargetSpawnOffsetDistance = 5000.0f;
};
