// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Save/FlightScenarioSave.h"

#include "FlightRecorderComponent.generated.h"

class UFlightDynamicsComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UAVSIMULATOR_API UFlightRecorderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightRecorderComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording")
	float RecordInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording")
	FString SaveSlotName = TEXT("TargetScenario_1");

	UFUNCTION(BlueprintCallable, Category = "Recording")
	void StartRecording();

	UFUNCTION(BlueprintCallable, Category = "Recording")
	void StopRecordingAndSave();

private:
	float CurrentRecordTime = 0.f;

	TArray<FFlightFrame> RecordedFrames;

	FTimerHandle RecordTimerHandle;

	void RecordFrame();
};
