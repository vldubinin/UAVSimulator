// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UAVSimulator/Entity/SimulatorMode.h"
#include "UAVSimulator/Entity/OnboardTargetMode.h"
#include "UAVSimulatorGameModeBase.generated.h"

class AAirplane;

UCLASS()
class UAVSIMULATOR_API AUAVSimulatorGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulator")
	ESimulatorMode CurrentSimulatorMode = ESimulatorMode::RecordTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulator")
	TSubclassOf<AAirplane> TargetAirplaneClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulator")
	TSubclassOf<AAirplane> TrackerAirplaneClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulator")
	FString ScenarioSlotName = TEXT("TargetScenario_1");

	/** Дистанція в см, на яку відтворювана траєкторія цілі зміщується вперед від стартової позиції трекера. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulator")
	float TargetSpawnOffsetDistance = 5000.0f;

	/** ZMQ PULL-адреса для команд атитюду в режимах PlaybackAndAutoTrack / AutoTrack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulator")
	FString AttitudeCommandEndpoint = TEXT("tcp://*:5556");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|VFX")
	bool bEnableVisualsForPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|VFX")
	bool bEnableVisualsForTarget = false;

	/** Надсилає поточні значення прапорців у UUAVSimulationSubsystem і розсилає їх усім літакам. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|VFX")
	void UpdateVisualSettings();

	/** Для якої ролі літака активна бортова камера; не залежить від CurrentSimulatorMode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Camera")
	EOnboardTargetMode OnboardCameraMode = EOnboardTargetMode::Drone;

	/** Надсилає поточні значення прапорців у UUAVSimulationSubsystem і розсилає їх усім літакам. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void UpdateCameraSettings();

	/** Для якої ролі літака активна шина сенсорів; не залежить від CurrentSimulatorMode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	EOnboardTargetMode SensorsMode = EOnboardTargetMode::Drone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorAltimeter = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorAttitudeIndicator = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorCameraInclination = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorLidar = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorCameraFrame = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorCameraAltitude = false;
		
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorSegmentationMask = false;
		
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorBBoxDetection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorPosition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorGeoPosition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorCesiumSurroundings = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorCustomSurroundings = false;

	/** Надсилає прапорці увімкнення сенсорів у UUAVSimulationSubsystem і розсилає їх усім літакам. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Sensors")
	void UpdateSensorSettings();

	/** Чи активна зона перешкод РЕБ (Electronic Warfare). Розташування й радіус беруться
	 *  напряму з AEWZoneActor, розміщеного в рівні (World Position). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|EW")
	bool bEWInterferenceEnabled = false;

	/** Читає AEWZoneActor з рівня і надсилає поточні налаштування РЕБ у UUAVSimulationSubsystem. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|EW")
	void UpdateEWSettings();

	/**
	 * Кількість кадрів, протягом яких сенсори мають "прогріватися" перед публікацією. Поки що
	 * лише налаштування тут — сама логіка прогріву ще не реалізована.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Global")
	int32 SensorWarmupFrameCount = 0;

	/** Спавнить актори та запускає симуляцію. Має викликатись явно (наприклад, з UI). */
	UFUNCTION(BlueprintCallable, Category = "Simulator")
	void StartSimulation();

	/** Знищує всі заспавнені літаки та скидає стан симуляції. */
	UFUNCTION(BlueprintCallable, Category = "Simulator")
	void StopSimulation();

	UPROPERTY(BlueprintReadOnly, Category = "Simulator")
	bool bSimulationStarted = false;
};
