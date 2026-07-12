// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "SyntheticDataSettingsSave.generated.h"

UCLASS()
class UAVSIMULATOR_API USyntheticDataSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite)
	FString SphericalContourBasePath;

	UPROPERTY(BlueprintReadWrite)
	FString KeyPointOutputJsonPath;

	UPROPERTY(BlueprintReadWrite)
	FString SceneObjectOutputJsonPath;

	UPROPERTY(BlueprintReadWrite)
	bool bEnableSensorSegmentationMask = false;

	UPROPERTY(BlueprintReadWrite)
	bool bEnableSensorBBoxDetection = false;
};
