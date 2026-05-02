// Fill out your copyright notice in the Description page of Project Settings.

#include "Airplane.h"
#include "Components/StaticMeshComponent.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"

AAirplane::AAirplane()
{
	PrimaryActorTick.bCanEverTick = true;

	FlightDynamics = CreateDefaultSubobject<UFlightDynamicsComponent>(TEXT("FlightDynamics"));
	// CameraComp is not a CDO — created dynamically in RefreshConfigurations when camera is enabled.
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
		Subsystem->OnVisualSettingsChanged.AddUObject(this, &AAirplane::RefreshConfigurations);
		Subsystem->OnCameraSettingsChanged.AddUObject(this, &AAirplane::RefreshConfigurations);
		RefreshConfigurations();
	}
}

void AAirplane::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshConfigurations();
}

void AAirplane::RefreshConfigurations()
{
	UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>();
	if (!Subsystem) return;

	const bool bIsPlayer = ActorHasTag(FName("Player"));
	const bool bIsTarget = ActorHasTag(FName("Target"));

	const bool bNiagaraActive = (bIsPlayer && Subsystem->bEnableVisualsForPlayer)
	                  || (bIsTarget && Subsystem->bEnableVisualsForTarget);

	const bool bCameraActive = (bIsPlayer && Subsystem->bEnableCameraForPlayer)
	                  || (bIsTarget && Subsystem->bEnableCameraForTarget);

	TArray<UAerodynamicSurfaceSC*> Surfaces;
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		Surface->SetNiagaraActive(bNiagaraActive);
	}

	// Create the camera component only when it is actually needed.
	if (bCameraActive && !CameraComp)
	{
		CameraComp = NewObject<UUAVCameraComponent>(this, TEXT("CameraComp"));
		CameraComp->RegisterComponent();
	}

	if (CameraComp)
	{
		CameraComp->SetCameraProcessingEnabled(bCameraActive);
	}

	// Widget is only relevant for the locally controlled (player) pawn.
	if (bCameraActive && IsLocallyControlled() && CameraWidgetClass && !CameraWidget)
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			CameraWidget = CreateWidget<UUserWidget>(PC, CameraWidgetClass);
			if (CameraWidget) CameraWidget->AddToViewport();
		}
	}
	else if (!bCameraActive && CameraWidget)
	{
		CameraWidget->RemoveFromParent();
		CameraWidget = nullptr;
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
