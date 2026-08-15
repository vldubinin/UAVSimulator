// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "GlobalSettingsSave.generated.h"

UCLASS()
class UAVSIMULATOR_API UGlobalSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite)
	int32 SensorWarmupFrameCount = 0;
};
