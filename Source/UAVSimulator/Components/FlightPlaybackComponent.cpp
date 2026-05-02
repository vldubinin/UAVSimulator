// Fill out your copyright notice in the Description page of Project Settings.

#include "FlightPlaybackComponent.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"

UFlightPlaybackComponent::UFlightPlaybackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Tick is driven by the playback state; start disabled and enable in StartPlayback.
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UFlightPlaybackComponent::StartPlayback()
{
	UFlightScenarioSave* Scenario = Cast<UFlightScenarioSave>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, /*UserIndex=*/0));

	if (!Scenario || Scenario->FlightFrames.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlightPlaybackComponent: failed to load scenario from slot '%s'"), *SaveSlotName);
		return;
	}

	LoadedScenario      = Scenario;
	CurrentPlaybackTime = 0.f;
	bIsPlaying          = true;
	SetComponentTickEnabled(true);

	AActor* Owner = GetOwner();
	if (!Owner) return;

	// Disable physics so playback drives transforms directly without fighting the solver.
	UStaticMeshComponent* Mesh = Owner->FindComponentByClass<UStaticMeshComponent>();
	if (Mesh)
	{
		Mesh->SetSimulatePhysics(false);
	}

	// Silence flight dynamics so it applies no forces while we replay transforms.
	UFlightDynamicsComponent* FlightDynamics = Owner->FindComponentByClass<UFlightDynamicsComponent>();
	if (FlightDynamics)
	{
		FlightDynamics->SetComponentTickEnabled(false);
		FlightDynamics->SetActive(false);
	}
}

void UFlightPlaybackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsPlaying || !LoadedScenario) return;

	CurrentPlaybackTime += DeltaTime;

	if (CurrentPlaybackTime >= LoadedScenario->TotalFlightDuration)
	{
		bIsPlaying = false;
		SetComponentTickEnabled(false);

		// Snap to the final recorded frame.
		const TArray<FFlightFrame>& Frames = LoadedScenario->FlightFrames;
		if (Frames.Num() > 0)
		{
			GetOwner()->SetActorLocationAndRotation(Frames.Last().Location + PlaybackOffset, Frames.Last().Rotation);
		}
		return;
	}

	// Find Frame B: first frame whose Timestamp is strictly past CurrentPlaybackTime.
	const TArray<FFlightFrame>& Frames = LoadedScenario->FlightFrames;
	int32 FrameBIndex = INDEX_NONE;
	for (int32 i = 0; i < Frames.Num(); ++i)
	{
		if (Frames[i].Timestamp > CurrentPlaybackTime)
		{
			FrameBIndex = i;
			break;
		}
	}

	// No upper bracket means we are before the first frame or the array is empty — nothing to draw yet.
	if (FrameBIndex <= 0) return;

	const FFlightFrame& FrameA = Frames[FrameBIndex - 1];
	const FFlightFrame& FrameB = Frames[FrameBIndex];

	const float Span  = FrameB.Timestamp - FrameA.Timestamp;
	const float Alpha = FMath::Clamp(
		Span > KINDA_SMALL_NUMBER ? (CurrentPlaybackTime - FrameA.Timestamp) / Span : 0.f,
		0.f, 1.f);

	const FVector   InterpLocation = FMath::Lerp(FrameA.Location, FrameB.Location, Alpha);
	const FRotator  InterpRotation = FQuat::Slerp(
		FrameA.Rotation.Quaternion(),
		FrameB.Rotation.Quaternion(),
		Alpha).Rotator();

	GetOwner()->SetActorLocationAndRotation(InterpLocation + PlaybackOffset, InterpRotation);
}
