// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Save/FlightScenarioSave.h"

#include "FlightPlaybackComponent.generated.h"

class UFlightDynamicsComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UAVSIMULATOR_API UFlightPlaybackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightPlaybackComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
	FString SaveSlotName = TEXT("TargetScenario_1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
	FVector PlaybackOffset = FVector::ZeroVector;

	UFUNCTION(BlueprintCallable, Category = "Playback")
	void StartPlayback();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY()
	UFlightScenarioSave* LoadedScenario = nullptr;

	float CurrentPlaybackTime = 0.f;
	bool  bIsPlaying          = false;
};
