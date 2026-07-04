// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UAVSimulator/Entity/SimulatorMode.h"
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

	/** Distance in cm to shift the replayed target trajectory ahead of the tracker's starting position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulator")
	float TargetSpawnOffsetDistance = 5000.0f;

	/** ZMQ PULL endpoint for attitude commands in PlaybackAndAutoTrack mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulator")
	FString AttitudeCommandEndpoint = TEXT("tcp://*:5556");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|VFX")
	bool bEnableVisualsForPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|VFX")
	bool bEnableVisualsForTarget = false;

	/** Pushes the current flag values to UUAVSimulationSubsystem and broadcasts to all airplanes. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|VFX")
	void UpdateVisualSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Camera")
	bool bEnableCameraForPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Camera")
	bool bEnableCameraForTarget = false;

	/** Pushes the current flag values to UUAVSimulationSubsystem and broadcasts to all airplanes. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void UpdateCameraSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Sensors")
	bool bEnableSensorAltimeter = false;

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

	/** Pushes sensor enable flags to UUAVSimulationSubsystem and broadcasts to all airplanes. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Sensors")
	void UpdateSensorSettings();

	/** Spawns actors and starts the simulation. Must be called explicitly (e.g., from the UI). */
	UFUNCTION(BlueprintCallable, Category = "Simulator")
	void StartSimulation();

	/** Destroys all spawned airplanes and resets simulation state. */
	UFUNCTION(BlueprintCallable, Category = "Simulator")
	void StopSimulation();

	UPROPERTY(BlueprintReadOnly, Category = "Simulator")
	bool bSimulationStarted = false;
};
