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

	// — Зоряне небо (StarsSphere / MI_Stars) — значення відповідають дефолтам матеріалу —
	UPROPERTY(BlueprintReadWrite)
	double StarsDensity = 200.0;

	UPROPERTY(BlueprintReadWrite)
	double StarsThreshold = 0.85;

	UPROPERTY(BlueprintReadWrite)
	double StarsPointSize = 0.008;

	UPROPERTY(BlueprintReadWrite)
	double StarsIntensity = 1000.0;

	// — Хмари (VolumetricCloud_0 / MI_VolumetricClouds) — значення відповідають дефолтам
	// m_SimpleVolumetricCloud_Inst —
	UPROPERTY(BlueprintReadWrite)
	double CloudsCoverage = -0.2;

	UPROPERTY(BlueprintReadWrite)
	double CloudsDensity = 0.008;

	UPROPERTY(BlueprintReadWrite)
	double CloudsSpeed = 1.0;

	UPROPERTY(BlueprintReadWrite)
	bool bTerrainSurfaceEnabled = true;

	UPROPERTY(BlueprintReadWrite)
	bool bEWInterferenceEnabled = false;

	/** Довгота зони РЕБ, градуси (глобальна геокоордината, не Unreal-одиниці). */
	UPROPERTY(BlueprintReadWrite)
	double EWLongitude = 0.0;

	/** Широта зони РЕБ, градуси (глобальна геокоордината, не Unreal-одиниці). */
	UPROPERTY(BlueprintReadWrite)
	double EWLatitude = 0.0;

	UPROPERTY(BlueprintReadWrite)
	double EWRadius = 5000.0;

	/** ARainEffectManager::RainIntensity — множник інтенсивності дощу (1 = базова,
	 *  0 = дощу нема взагалі — єдиний вимикач, окремого bool-прапорця нема). */
	UPROPERTY(BlueprintReadWrite)
	double RainIntensity = 1.0;
};
