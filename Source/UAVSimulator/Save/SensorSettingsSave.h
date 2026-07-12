// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "SensorSettingsSave.generated.h"

UCLASS()
class UAVSIMULATOR_API USensorSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite)
	bool bEnableSensorCameraFrame = false;

	UPROPERTY(BlueprintReadWrite)
	bool bEnableSensorAltimeter = false;

	UPROPERTY(BlueprintReadWrite)
	bool bEnableSensorCameraInclination = false;

	UPROPERTY(BlueprintReadWrite)
	bool bEnableSensorLidar = false;

	UPROPERTY(BlueprintReadWrite)
	bool bEnableSensorCameraAltitude = false;

	UPROPERTY(BlueprintReadWrite)
	bool bEnableSensorPosition = false;
};
