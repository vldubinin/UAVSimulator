// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "EnvironmentSettingsSave.generated.h"

UCLASS()
class UAVSIMULATOR_API UEnvironmentSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite)
	double OriginLatitude = 0.0;

	UPROPERTY(BlueprintReadWrite)
	double OriginLongitude = 0.0;

	UPROPERTY(BlueprintReadWrite)
	double OriginHeight = 0.0;

	UPROPERTY(BlueprintReadWrite)
	double TimeZone = 0.0;

	UPROPERTY(BlueprintReadWrite)
	double SolarTime = 0.0;

	UPROPERTY(BlueprintReadWrite)
	bool bTerrainSurfaceEnabled = true;
};
