// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "UAVSimulator/Entity/ControlInputState.h"

#include "FlightScenarioSave.generated.h"

USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FFlightFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	float Timestamp = 0.f;

	UPROPERTY(BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite)
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite)
	FControlInputState ControlState;
};

UCLASS()
class UAVSIMULATOR_API UFlightScenarioSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite)
	TArray<FFlightFrame> FlightFrames;

	UPROPERTY(BlueprintReadWrite)
	FString AirplaneClassPath;

	UPROPERTY(BlueprintReadWrite)
	float TotalFlightDuration = 0.f;
};
