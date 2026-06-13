// Fill out your copyright notice in the Description page of Project Settings.

#include "Airplane.h"
#include "Components/StaticMeshComponent.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "UAVSimulator/UAVSimulatorPlayerController.h"
#include "UAVSimulator/UI/CameraViewWidget.h"
#include "UAVSimulator/Components/CameraAltitudeComponent.h"
#include "UAVSimulator/Components/CameraFrameComponent.h"
#include "UAVSimulator/Components/SegmentationMaskCameraComponent.h"
#include "UAVSimulator/Components/BBoxDetectionComponent.h"
#include "UAVSimulator/Components/AltimeterComponent.h"
#include "UAVSimulator/Components/CameraInclinationComponent.h"
#include "UAVSimulator/Components/LidarComponent.h"

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
		Subsystem->OnSensorSettingsChanged.AddUObject(this, &AAirplane::RefreshSensorSettings);
		RefreshConfigurations();
		RefreshSensorSettings();
	}
}

void AAirplane::CleanupWidgets()
{
	if (CameraWidget)
	{
		CameraWidget->RemoveFromParent();
		CameraWidget = nullptr;
	}
}

void AAirplane::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupWidgets();
	Super::EndPlay(EndPlayReason);
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
			if (CameraWidget)
			{
				if (UCameraViewWidget* CameraViewWidget = Cast<UCameraViewWidget>(CameraWidget))
				{
					CameraViewWidget->SetAirplane(this);
				}
				CameraWidget->AddToViewport();
				if (AUAVSimulatorPlayerController* UAVPC = Cast<AUAVSimulatorPlayerController>(PC))
				{
					UAVPC->RegisterCameraWidget(CameraWidget);
				}
			}
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

void AAirplane::RefreshSensorSettings()
{
	UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>();
	if (!Subsystem) return;

	if (UAltimeterComponent* C = FindComponentByClass<UAltimeterComponent>())
		C->bSensorEnabled = Subsystem->bEnableSensorAltimeter;

	if (UCameraInclinationComponent* C = FindComponentByClass<UCameraInclinationComponent>())
		C->bSensorEnabled = Subsystem->bEnableSensorCameraInclination;

	if (ULidarComponent* C = FindComponentByClass<ULidarComponent>())
		C->bSensorEnabled = Subsystem->bEnableSensorLidar;

	if (UCameraFrameComponent* C = FindComponentByClass<UCameraFrameComponent>())
		C->bSensorEnabled = Subsystem->bEnableSensorCameraFrame;

	if (UCameraAltitudeComponent* C = FindComponentByClass<UCameraAltitudeComponent>())
		C->bSensorEnabled = Subsystem->bEnableSensorCameraAltitude;

	if (USegmentationMaskCameraComponent* C = FindComponentByClass<USegmentationMaskCameraComponent>())
		C->bSensorEnabled = Subsystem->bEnableSensorSegmentationMask;

	if (UBBoxDetectionComponent* C = FindComponentByClass<UBBoxDetectionComponent>())
		C->bSensorEnabled = Subsystem->bEnableSensorBBoxDetection;
}

UTexture2D* AAirplane::GetCameraOutputTexture() const
{
	return CameraComp ? CameraComp->OutputTexture : nullptr;
}
