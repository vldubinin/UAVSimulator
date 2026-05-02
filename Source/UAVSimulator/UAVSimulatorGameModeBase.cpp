// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAVSimulatorGameModeBase.h"
#include "UAVSimulator/Actor/Airplane.h"
#include "UAVSimulator/Components/FlightRecorderComponent.h"
#include "UAVSimulator/Components/FlightPlaybackComponent.h"
#include "UAVSimulator/Save/FlightScenarioSave.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"

void AUAVSimulatorGameModeBase::UpdateVisualSettings()
{
	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		Subsystem->SetVisualSettings(bEnableVisualsForPlayer, bEnableVisualsForTarget);
	}
}

void AUAVSimulatorGameModeBase::UpdateCameraSettings()
{
	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		Subsystem->SetCameraSettings(bEnableCameraForPlayer, bEnableCameraForTarget);
	}
}

void AUAVSimulatorGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	// Push all flags to the subsystem before any actors are spawned so that
	// RefreshConfigurations() called during BeginPlay/PossessedBy reads the correct values.
	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		Subsystem->bEnableVisualsForPlayer = bEnableVisualsForPlayer;
		Subsystem->bEnableVisualsForTarget = bEnableVisualsForTarget;
		Subsystem->bEnableCameraForPlayer  = bEnableCameraForPlayer;
		Subsystem->bEnableCameraForTarget  = bEnableCameraForTarget;
	}

	AActor* PlayerStartActor = UGameplayStatics::GetActorOfClass(GetWorld(), APlayerStart::StaticClass());
	FTransform SpawnTransform = PlayerStartActor
		? PlayerStartActor->GetActorTransform()
		: FTransform::Identity;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (CurrentSimulatorMode == ESimulatorMode::RecordTarget)
	{
		if (!TargetAirplaneClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("UAVSimulatorGameModeBase: TargetAirplaneClass is not set."));
			return;
		}

		AAirplane* TargetAirplane = GetWorld()->SpawnActor<AAirplane>(TargetAirplaneClass, SpawnTransform, SpawnParams);
		if (!TargetAirplane) return;

		TargetAirplane->Tags.Add(FName("Player"));

		UFlightRecorderComponent* Recorder = NewObject<UFlightRecorderComponent>(TargetAirplane);
		Recorder->SaveSlotName = ScenarioSlotName;
		Recorder->RegisterComponent();
		Recorder->StartRecording();
	}
	else if (CurrentSimulatorMode == ESimulatorMode::PlaybackAndTrack)
	{
		if (!TargetAirplaneClass || !TrackerAirplaneClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("UAVSimulatorGameModeBase: TargetAirplaneClass or TrackerAirplaneClass is not set."));
			return;
		}

		UFlightScenarioSave* LoadedScenario = Cast<UFlightScenarioSave>(
			UGameplayStatics::LoadGameFromSlot(ScenarioSlotName, /*UserIndex=*/0));

		if (!LoadedScenario || LoadedScenario->FlightFrames.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("UAVSimulatorGameModeBase: Failed to load scenario from slot '%s'."), *ScenarioSlotName);
			return;
		}

		const FFlightFrame& FirstFrame   = LoadedScenario->FlightFrames[0];
		const FVector       InitialLocation = FirstFrame.Location;
		const FRotator      InitialRotation = FirstFrame.Rotation;

		// Tracker starts exactly where the recording began.
		AAirplane* TrackerAirplane = GetWorld()->SpawnActor<AAirplane>(
			TrackerAirplaneClass, InitialLocation, InitialRotation, SpawnParams);
		if (TrackerAirplane) TrackerAirplane->Tags.Add(FName("Player"));

		// Target replays its recorded path shifted forward along the initial heading.
		const FVector Offset = InitialRotation.Vector() * TargetSpawnOffsetDistance;

		AAirplane* TargetAirplane = GetWorld()->SpawnActor<AAirplane>(
			TargetAirplaneClass, InitialLocation + Offset, InitialRotation, SpawnParams);
		if (TargetAirplane) TargetAirplane->Tags.Add(FName("Target"));

		if (TargetAirplane)
		{
			UFlightPlaybackComponent* Playback = NewObject<UFlightPlaybackComponent>(TargetAirplane);
			Playback->SaveSlotName   = ScenarioSlotName;
			Playback->PlaybackOffset = Offset;
			Playback->RegisterComponent();
			Playback->StartPlayback();
		}

		// Give the player controller the tracker so the camera follows the chasing airplane.
		if (TrackerAirplane)
		{
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				PC->Possess(TrackerAirplane);
			}
		}
	}
	else if (CurrentSimulatorMode == ESimulatorMode::Playback)
	{
		if (!TargetAirplaneClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("UAVSimulatorGameModeBase: TargetAirplaneClass is not set."));
			return;
		}

		UFlightScenarioSave* LoadedScenario = Cast<UFlightScenarioSave>(
			UGameplayStatics::LoadGameFromSlot(ScenarioSlotName, /*UserIndex=*/0));

		if (!LoadedScenario || LoadedScenario->FlightFrames.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("UAVSimulatorGameModeBase: Failed to load scenario from slot '%s'."), *ScenarioSlotName);
			return;
		}

		const FFlightFrame& FirstFrame = LoadedScenario->FlightFrames[0];
		const FVector       InitialLocation = FirstFrame.Location;
		const FRotator      InitialRotation = FirstFrame.Rotation;

		AAirplane* TargetAirplane = GetWorld()->SpawnActor<AAirplane>(
			TargetAirplaneClass, InitialLocation, InitialRotation, SpawnParams);
		if (TargetAirplane) TargetAirplane->Tags.Add(FName("Target"));

		if (TargetAirplane)
		{
			UFlightPlaybackComponent* Playback = NewObject<UFlightPlaybackComponent>(TargetAirplane);
			Playback->SaveSlotName = ScenarioSlotName;
			Playback->PlaybackOffset = FVector();
			Playback->RegisterComponent();
			Playback->StartPlayback();
		}
	}

	// Broadcast AFTER all actors are spawned and possessed so that
	// every airplane's BeginPlay has already subscribed to the delegates.
	// Camera is broadcast first since widget creation depends on it.
	UpdateCameraSettings();
	UpdateVisualSettings();
}
