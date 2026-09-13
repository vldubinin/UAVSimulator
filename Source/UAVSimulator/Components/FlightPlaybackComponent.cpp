// Заповніть примітку про авторські права на сторінці Description в Project Settings.

#include "FlightPlaybackComponent.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"

UFlightPlaybackComponent::UFlightPlaybackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Тік керується станом відтворення; починаємо вимкненим і вмикаємо в StartPlayback.
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UFlightPlaybackComponent::StartPlayback()
{
	UFlightScenarioSave* Scenario = Cast<UFlightScenarioSave>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, /*UserIndex=*/0));

	if (!Scenario || Scenario->FlightFrames.Num() == 0)
	{
		return;
	}

	LoadedScenario      = Scenario;
	CurrentPlaybackTime = 0.f;
	bIsPlaying          = true;
	SetComponentTickEnabled(true);

	AActor* Owner = GetOwner();
	if (!Owner) return;

	// Вимикаємо фізику, щоб відтворення напряму керувало трансформаціями, не борючись із солвером.
	UStaticMeshComponent* Mesh = Owner->FindComponentByClass<UStaticMeshComponent>();
	if (Mesh)
	{
		Mesh->SetSimulatePhysics(false);
	}

	// Глушимо політну динаміку, щоб під час відтворення трансформацій вона не застосовувала жодних сил.
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

		// Фіксуємось на останньому записаному кадрі.
		const TArray<FFlightFrame>& Frames = LoadedScenario->FlightFrames;
		if (Frames.Num() > 0)
		{
			GetOwner()->SetActorLocationAndRotation(Frames.Last().Location + PlaybackOffset, Frames.Last().Rotation);
		}
		return;
	}

	// Шукаємо кадр B: перший кадр, чий Timestamp строго перевищує CurrentPlaybackTime.
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

	// Відсутність верхньої межі означає, що ми перед першим кадром або масив порожній — малювати ще нічого.
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
