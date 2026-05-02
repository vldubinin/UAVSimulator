// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UAVSimulatorGameModeBase.generated.h"

class AAirplane;

UENUM(BlueprintType)
enum class ESimulatorMode : uint8
{
	RecordTarget     UMETA(DisplayName = "Record Target"),
	PlaybackAndTrack UMETA(DisplayName = "Playback and Track"),
	Playback UMETA(DisplayName = "Playback"),
};

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
};
