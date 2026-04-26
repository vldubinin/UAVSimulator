// Fill out your copyright notice in the Description page of Project Settings.

#include "Airplane.h"
#include "Components/StaticMeshComponent.h"

AAirplane::AAirplane()
{
	PrimaryActorTick.bCanEverTick = true;

	CameraComp    = CreateDefaultSubobject<UUAVCameraComponent>(TEXT("CameraComp"));
	FlightDynamics = CreateDefaultSubobject<UFlightDynamicsComponent>(TEXT("FlightDynamics"));
}

void AAirplane::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UStaticMeshComponent* Mesh = FindComponentByClass<UStaticMeshComponent>();
	FlightDynamics->UpdateEditorVisualization(Mesh);
}

void AAirplane::BeginPlay()
{
	Super::BeginPlay();
}

void AAirplane::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (CameraComp) CameraComp->ProcessFrame();
}

UTexture2D* AAirplane::GetCameraOutputTexture() const
{
	return CameraComp ? CameraComp->OutputTexture : nullptr;
}
