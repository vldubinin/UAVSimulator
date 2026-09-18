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

	// — Експозиція бортової камери ————————————————————————————————————————————
	UPROPERTY(BlueprintReadWrite)
	bool bCameraManualExposure = true;

	UPROPERTY(BlueprintReadWrite)
	float CameraISO = 100.0f;

	UPROPERTY(BlueprintReadWrite)
	float CameraShutterSpeed = 60.0f;

	UPROPERTY(BlueprintReadWrite)
	float CameraApertureFStop = 4.0f;

	UPROPERTY(BlueprintReadWrite)
	float CameraExposureBias = -3.4f;
};
