// Fill out your copyright notice in the Description page of Project Settings.

#include "Airplane.h"
#include "Components/StaticMeshComponent.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "UAVSimulator/Components/FlightPlaybackComponent.h"

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

	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		Subsystem->OnVisualSettingsChanged.AddUObject(this, &AAirplane::RefreshVisualEffects);
		RefreshVisualEffects();
	}
}

void AAirplane::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshVisualEffects();
}

void AAirplane::RefreshVisualEffects()
{
	UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>();
	if (!Subsystem) return;

	// Role check: player = locally controlled pawn; target = running a recorded playback.
	const bool bIsPlayer = IsLocallyControlled();
	const bool bIsTarget = FindComponentByClass<UFlightPlaybackComponent>() != nullptr;

	const bool bActive = (bIsPlayer && Subsystem->bEnableVisualsForPlayer)
	                  || (bIsTarget && Subsystem->bEnableVisualsForTarget);

	TArray<UAerodynamicSurfaceSC*> Surfaces;
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		Surface->SetNiagaraActive(bActive);
	}

	if (CameraComp)
	{
		CameraComp->SetCameraProcessingEnabled(bIsPlayer);
	}
}

void AAirplane::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

UTexture2D* AAirplane::GetCameraOutputTexture() const
{
	if (IsLocallyControlled() && CameraComp)
	{
		return CameraComp->OutputTexture;
	}
	return nullptr;
}
