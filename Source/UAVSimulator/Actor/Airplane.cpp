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

	if (bCameraActive && CameraWidgetClass && !CameraWidget)
	{
		// For a locally controlled pawn use its own PC; for the target use the world's first PC
		// so the tracking player sees the target camera feed on their HUD.
		APlayerController* PC = IsLocallyControlled()
			? Cast<APlayerController>(GetController())
			: GetWorld()->GetFirstPlayerController();

		if (PC)
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
	return CameraComp ? CameraComp->OutputTexture : nullptr;
}
