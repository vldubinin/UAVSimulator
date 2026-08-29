// Fill out your copyright notice in the Description page of Project Settings.

#include "Airplane.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "UAVSimulator/Interfaces/PilotInputSource.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "UAVSimulator/UAVSimulatorPlayerController.h"
#include "UAVSimulator/UI/CameraViewWidget.h"
#include "UAVSimulator/UI/AirplaneTelemetryWidget.h"
#include "UAVSimulator/Components/CameraAltitudeComponent.h"
#include "UAVSimulator/Components/CameraFrameComponent.h"
#include "UAVSimulator/Components/SegmentationMaskCameraComponent.h"
#include "UAVSimulator/Components/BBoxDetectionComponent.h"
#include "UAVSimulator/Components/AltimeterComponent.h"
#include "UAVSimulator/Components/CameraInclinationComponent.h"
#include "UAVSimulator/Components/LidarComponent.h"
#include "UAVSimulator/Components/DronePositionComponent.h"
#include "UAVSimulator/Components/GeoPositionDroneComponent.h"
#include "UAVSimulator/Components/CesiumSurroundingsScannerComponent.h"
#include "UAVSimulator/Components/CustomSurroundingsScannerComponent.h"

namespace
{
	/** Resolves an EOnboardTargetMode selection against an airplane's role tags. AutoTracker counts as Drone. */
	bool IsRoleActiveForOnboardMode(EOnboardTargetMode Mode, bool bIsPlayer, bool bIsTarget, bool bIsAutoTracker)
	{
		switch (Mode)
		{
		case EOnboardTargetMode::Drone:  return bIsPlayer || bIsAutoTracker;
		case EOnboardTargetMode::Target: return bIsTarget;
		case EOnboardTargetMode::None:   return false;
		}
		return false;
	}
}

AAirplane::AAirplane()
{
	PrimaryActorTick.bCanEverTick = true;

	FlightDynamics  = CreateDefaultSubobject<UFlightDynamicsComponent>(TEXT("FlightDynamics"));
	AttitudeControl = CreateDefaultSubobject<UAttitudeControlComponent>(TEXT("AttitudeControl"));

	// Керування пілота: координатор + джерела вводу (кожне реалізує IPilotInputSource).
	PilotInput    = CreateDefaultSubobject<UPilotInputComponent>(TEXT("PilotInput"));
	KeyboardInput = CreateDefaultSubobject<UKeyboardPilotInputComponent>(TEXT("KeyboardInput"));
	GamepadInput  = CreateDefaultSubobject<UGamepadPilotInputComponent>(TEXT("GamepadInput"));

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

	// Порядок за кадр для керування:
	//   AAirplane::Tick -> UAttitudeControlComponent (ZMQ+PID, лише обчислення)
	//                   -> UPilotInputComponent (зведення + ЄДИНИЙ запис у FlightDynamics)
	//                   -> UFlightDynamicsComponent (споживання; обнуляє ControlState наприкінці тіку)
	if (PilotInput)
	{
		PilotInput->AddTickPrerequisiteActor(this);
		if (AttitudeControl) PilotInput->AddTickPrerequisiteComponent(AttitudeControl);
	}
	if (PilotInput && FlightDynamics)
	{
		FlightDynamics->AddTickPrerequisiteComponent(PilotInput);
	}

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

	if (TelemetryWidget)
	{
		TelemetryWidget->RemoveFromParent();
		TelemetryWidget = nullptr;
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

void AAirplane::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UE_LOG(LogUAV, Log, TEXT("AAirplane::SetupPlayerInputComponent на %s — InputComponent %s"),
		*GetName(), PlayerInputComponent ? TEXT("валідний") : TEXT("NULL"));

	// Кожне джерело вводу прив'язує власні осі/клавіші. Актор лишається агностичним
	// до конкретних пристроїв — так само, як SensorBusComponent агностичний до сенсорів.
	int32 SourceCount = 0;
	for (UActorComponent* Comp : GetComponents())
	{
		if (Comp && Comp->Implements<UPilotInputSource>())
		{
			if (IPilotInputSource* Source = Cast<IPilotInputSource>(Comp))
			{
				UE_LOG(LogUAV, Log, TEXT("  BindInput -> джерело '%s'"), *Source->GetInputSourceId().ToString());
				Source->BindInput(PlayerInputComponent);
				++SourceCount;
			}
		}
	}
	UE_LOG(LogUAV, Log, TEXT("AAirplane::SetupPlayerInputComponent — прив'язано джерел: %d"), SourceCount);
}

void AAirplane::RefreshConfigurations()
{
	UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>();
	if (!Subsystem) return;

	const bool bIsPlayer      = ActorHasTag(FName("Player"));
	const bool bIsTarget      = ActorHasTag(FName("Target"));
	const bool bIsAutoTracker = ActorHasTag(FName("AutoTracker"));

	const bool bNiagaraActive = (bIsPlayer      && Subsystem->bEnableVisualsForPlayer)
	                  || (bIsTarget      && Subsystem->bEnableVisualsForTarget)
	                  || (bIsAutoTracker && Subsystem->bEnableVisualsForPlayer);

	const bool bCameraActive = IsRoleActiveForOnboardMode(Subsystem->OnboardCameraMode, bIsPlayer, bIsTarget, bIsAutoTracker);

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

	// Telemetry HUD only ever belongs to the pawn actually being flown right now — unlike the
	// camera widget, it's never shown for a target/tracker on someone else's PC.
	if (TelemetryWidgetClass && !TelemetryWidget && IsLocallyControlled())
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			TelemetryWidget = CreateWidget<UUserWidget>(PC, TelemetryWidgetClass);
			if (TelemetryWidget)
			{
				if (UAirplaneTelemetryWidget* TelemetryView = Cast<UAirplaneTelemetryWidget>(TelemetryWidget))
				{
					TelemetryView->SetAirplane(this);
				}
				TelemetryWidget->AddToViewport();
			}
		}
	}
	else if (!IsLocallyControlled() && TelemetryWidget)
	{
		TelemetryWidget->RemoveFromParent();
		TelemetryWidget = nullptr;
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

	const bool bIsPlayer      = ActorHasTag(FName("Player"));
	const bool bIsTarget      = ActorHasTag(FName("Target"));
	const bool bIsAutoTracker = ActorHasTag(FName("AutoTracker"));
	const bool bSensorsActive = IsRoleActiveForOnboardMode(Subsystem->SensorsMode, bIsPlayer, bIsTarget, bIsAutoTracker);

	if (UAltimeterComponent* C = FindComponentByClass<UAltimeterComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorAltimeter;

	if (UCameraInclinationComponent* C = FindComponentByClass<UCameraInclinationComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorCameraInclination;

	if (ULidarComponent* C = FindComponentByClass<ULidarComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorLidar;

	if (UCameraFrameComponent* C = FindComponentByClass<UCameraFrameComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorCameraFrame;

	if (UCameraAltitudeComponent* C = FindComponentByClass<UCameraAltitudeComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorCameraAltitude;

	if (USegmentationMaskCameraComponent* C = FindComponentByClass<USegmentationMaskCameraComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorSegmentationMask;

	if (UBBoxDetectionComponent* C = FindComponentByClass<UBBoxDetectionComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorBBoxDetection;

	if (UDronePositionComponent* C = FindComponentByClass<UDronePositionComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorPosition;

	if (UGeoPositionDroneComponent* C = FindComponentByClass<UGeoPositionDroneComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorGeoPosition;

	if (UCesiumSurroundingsScannerComponent* C = FindComponentByClass<UCesiumSurroundingsScannerComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorCesiumSurroundings;

	if (UCustomSurroundingsScannerComponent* C = FindComponentByClass<UCustomSurroundingsScannerComponent>())
		C->bSensorEnabled = bSensorsActive && Subsystem->bEnableSensorCustomSurroundings;
}

UTexture2D* AAirplane::GetCameraOutputTexture() const
{
	return CameraComp ? CameraComp->OutputTexture : nullptr;
}

float AAirplane::GetAirspeedMs() const
{
	return FlightDynamics ? FlightDynamics->GetAirspeed() * 3.6f: 0.0f;
}

float AAirplane::GetAirspeedKmh() const
{
	return GetAirspeedMs() * 3.6f;
}
