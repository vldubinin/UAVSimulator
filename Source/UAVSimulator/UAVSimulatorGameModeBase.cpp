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

void AUAVSimulatorGameModeBase::UpdateSensorSettings()
{
	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		Subsystem->SetSensorSettings(bEnableSensorAltimeter, bEnableSensorCameraInclination, bEnableSensorLidar);
	}
}

void AUAVSimulatorGameModeBase::StopSimulation()
{
	if (!bSimulationStarted) return;
	bSimulationStarted = false;

	TArray<AActor*> Airplanes;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAirplane::StaticClass(), Airplanes);
	for (AActor* Actor : Airplanes)
	{
		if (AAirplane* Airplane = Cast<AAirplane>(Actor))
		{
			Airplane->CleanupWidgets();
		}
		Actor->Destroy();
	}
}

void AUAVSimulatorGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	// Push flags so subsystem is ready before StartSimulation() is called from the UI.
	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		Subsystem->CurrentSimulatorMode    = CurrentSimulatorMode;
		Subsystem->bEnableVisualsForPlayer = bEnableVisualsForPlayer;
		Subsystem->bEnableVisualsForTarget = bEnableVisualsForTarget;
		Subsystem->bEnableCameraForPlayer          = bEnableCameraForPlayer;
		Subsystem->bEnableCameraForTarget          = bEnableCameraForTarget;
		Subsystem->bEnableSensorAltimeter          = bEnableSensorAltimeter;
		Subsystem->bEnableSensorCameraInclination  = bEnableSensorCameraInclination;
		Subsystem->bEnableSensorLidar              = bEnableSensorLidar;
	}
}

void AUAVSimulatorGameModeBase::StartSimulation()
{
	if (bSimulationStarted) return;
	bSimulationStarted = true;

	// Re-push current settings (may have been edited via UI before Start).
	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		Subsystem->CurrentSimulatorMode    = CurrentSimulatorMode;
		Subsystem->bEnableVisualsForPlayer = bEnableVisualsForPlayer;
		Subsystem->bEnableVisualsForTarget = bEnableVisualsForTarget;
		Subsystem->bEnableCameraForPlayer          = bEnableCameraForPlayer;
		Subsystem->bEnableCameraForTarget          = bEnableCameraForTarget;
		Subsystem->bEnableSensorAltimeter          = bEnableSensorAltimeter;
		Subsystem->bEnableSensorCameraInclination  = bEnableSensorCameraInclination;
		Subsystem->bEnableSensorLidar              = bEnableSensorLidar;
	}

	AActor* PlayerStartActor = UGameplayStatics::GetActorOfClass(GetWorld(), APlayerStart::StaticClass());
	FTransform SpawnTransform = PlayerStartActor
		? PlayerStartActor->GetActorTransform()
		: FTransform::Identity;

	constexpr ESpawnActorCollisionHandlingMethod AlwaysSpawn = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (CurrentSimulatorMode == ESimulatorMode::RecordTarget)
	{
		if (!TargetAirplaneClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("UAVSimulatorGameModeBase: TargetAirplaneClass is not set."));
			return;
		}

		AAirplane* TargetAirplane = GetWorld()->SpawnActorDeferred<AAirplane>(
			TargetAirplaneClass, SpawnTransform, nullptr, nullptr, AlwaysSpawn);
		if (!TargetAirplane) return;
		TargetAirplane->Tags.Add(FName("Player"));
		TargetAirplane->FinishSpawning(SpawnTransform);

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

		const FFlightFrame& FirstFrame      = LoadedScenario->FlightFrames[0];
		const FVector       InitialLocation = FirstFrame.Location;
		const FRotator      InitialRotation = FirstFrame.Rotation;
		const FVector       Offset          = InitialRotation.Vector() * TargetSpawnOffsetDistance;

		const FTransform TrackerTransform(InitialRotation, InitialLocation);
		AAirplane* TrackerAirplane = GetWorld()->SpawnActorDeferred<AAirplane>(
			TrackerAirplaneClass, TrackerTransform, nullptr, nullptr, AlwaysSpawn);
		if (TrackerAirplane) TrackerAirplane->Tags.Add(FName("Player"));

		const FTransform TargetTransform(InitialRotation, InitialLocation + Offset);
		AAirplane* TargetAirplane = GetWorld()->SpawnActorDeferred<AAirplane>(
			TargetAirplaneClass, TargetTransform, nullptr, nullptr, AlwaysSpawn);
		if (TargetAirplane) TargetAirplane->Tags.Add(FName("Target"));

		if (TrackerAirplane) TrackerAirplane->FinishSpawning(TrackerTransform);
		if (TargetAirplane)  TargetAirplane->FinishSpawning(TargetTransform);

		if (TargetAirplane)
		{
			UFlightPlaybackComponent* Playback = NewObject<UFlightPlaybackComponent>(TargetAirplane);
			Playback->SaveSlotName   = ScenarioSlotName;
			Playback->PlaybackOffset = Offset;
			Playback->RegisterComponent();
			Playback->StartPlayback();
		}

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

		const FFlightFrame& FirstFrame      = LoadedScenario->FlightFrames[0];
		const FVector       InitialLocation = FirstFrame.Location;
		const FRotator      InitialRotation = FirstFrame.Rotation;

		const FTransform TargetTransform(InitialRotation, InitialLocation);
		AAirplane* TargetAirplane = GetWorld()->SpawnActorDeferred<AAirplane>(
			TargetAirplaneClass, TargetTransform, nullptr, nullptr, AlwaysSpawn);
		if (TargetAirplane) TargetAirplane->Tags.Add(FName("Target"));
		if (TargetAirplane) TargetAirplane->FinishSpawning(TargetTransform);

		if (TargetAirplane)
		{
			UFlightPlaybackComponent* Playback = NewObject<UFlightPlaybackComponent>(TargetAirplane);
			Playback->SaveSlotName   = ScenarioSlotName;
			Playback->PlaybackOffset = FVector();
			Playback->RegisterComponent();
			Playback->StartPlayback();
		}
	}

	// Broadcast AFTER all actors are spawned and possessed.
	UpdateCameraSettings();
	UpdateVisualSettings();
	UpdateSensorSettings();
}
