// Fill out your copyright notice in the Description page of Project Settings.

#include "FlightRecorderComponent.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"

UFlightRecorderComponent::UFlightRecorderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFlightRecorderComponent::StartRecording()
{
	RecordedFrames.Empty();
	CurrentRecordTime = 0.f;

	GetWorld()->GetTimerManager().SetTimer(
		RecordTimerHandle,
		this,
		&UFlightRecorderComponent::RecordFrame,
		RecordInterval,
		/*bLoop=*/true);
}

void UFlightRecorderComponent::RecordFrame()
{
	CurrentRecordTime += RecordInterval;

	AActor* Owner = GetOwner();
	if (!Owner) return;

	FFlightFrame Frame;
	Frame.Timestamp = CurrentRecordTime;
	Frame.Location  = Owner->GetActorLocation();
	Frame.Rotation  = Owner->GetActorRotation();

	UFlightDynamicsComponent* FlightDynamics = Owner->FindComponentByClass<UFlightDynamicsComponent>();
	if (FlightDynamics)
	{
		Frame.ControlState = FlightDynamics->GetControlState();
	}

	RecordedFrames.Add(Frame);
}

void UFlightRecorderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRecordingAndSave();
	Super::EndPlay(EndPlayReason);
}

void UFlightRecorderComponent::StopRecordingAndSave()
{
	GetWorld()->GetTimerManager().ClearTimer(RecordTimerHandle);

	UFlightScenarioSave* SaveObject = Cast<UFlightScenarioSave>(
		UGameplayStatics::CreateSaveGameObject(UFlightScenarioSave::StaticClass()));

	SaveObject->FlightFrames       = RecordedFrames;
	SaveObject->TotalFlightDuration = CurrentRecordTime;

	AActor* Owner = GetOwner();
	if (Owner)
	{
		SaveObject->AirplaneClassPath = Owner->GetClass()->GetPathName();
	}

	UGameplayStatics::SaveGameToSlot(SaveObject, SaveSlotName, /*UserIndex=*/0);
}
