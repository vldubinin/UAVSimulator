// Fill out your copyright notice in the Description page of Project Settings.

#include "PhysicalAirplane.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"

APhysicalAirplane::APhysicalAirplane()
{
	PrimaryActorTick.bCanEverTick = true;

	CameraComp   = CreateDefaultSubobject<UUAVCameraComponent>(TEXT("CameraComp"));
	PhysicsState = CreateDefaultSubobject<UUAVPhysicsStateComponent>(TEXT("PhysicsState"));
}

void APhysicalAirplane::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ControlState = FControlInputState();
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);

	UStaticMeshComponent* Mesh = FindComponentByClass<UStaticMeshComponent>();
	FVector CoM = Mesh ? Mesh->GetCenterOfMass() : FVector::ZeroVector;

	TArray<UControlSurfaceSC*> AllControlSurfaces;
	GetComponents<UControlSurfaceSC>(AllControlSurfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		Surface->OnConstruction(CoM, AllControlSurfaces);
	}
}

void APhysicalAirplane::BeginPlay()
{
	Super::BeginPlay();

	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);

	ControlState = FControlInputState();
	GetComponents<UControlSurfaceSC>(ControlSurfaces);
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);

	UStaticMeshComponent* Mesh = FindComponentByClass<UStaticMeshComponent>();
	FVector CoM = Mesh ? Mesh->GetCenterOfMass() : FVector::ZeroVector;
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		Surface->OnConstruction(CoM, ControlSurfaces);
	}
}

void APhysicalAirplane::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UE_LOG(LogUAV, Verbose, TEXT("### TICK ###"));

	if (CameraComp) CameraComp->ProcessFrame();

	PhysicsState->Update();

	UStaticMeshComponent* Mesh = FindComponentByClass<UStaticMeshComponent>();
	if (Mesh && Mesh->IsSimulatingPhysics())
	{
		FAerodynamicForce TotalForce;
		for (UAerodynamicSurfaceSC* Surface : Surfaces)
		{
			FAerodynamicForce SurfaceForce = Surface->CalculateForcesOnSurface(
				PhysicsState->GetCenterOfMass(),
				PhysicsState->GetLinearVelocity(),
				PhysicsState->GetAngularVelocity(),
				PhysicsState->GetAirflowDirection(),
				ControlState);
			TotalForce.PositionalForce += SurfaceForce.PositionalForce;
			TotalForce.RotationalForce += SurfaceForce.RotationalForce;
		}

		Mesh->AddForce(TotalForce.PositionalForce);
		Mesh->AddTorqueInRadians(TotalForce.RotationalForce);

		if (ThrottlePercent > 0.0f)
		{
			const float MaxThrust = 1500000.f;
			Mesh->AddForce(GetActorForwardVector() * MaxThrust * ThrottlePercent);
		}

		UE_LOG(LogUAV, Log, TEXT("%s Location: %s"), *GetName(), *GetActorLocation().ToString());
	}

	ControlState = FControlInputState();
}

void APhysicalAirplane::UpdateAileronControl(float LeftAileronAngleValue, float RightAileronAngleValue)
{
	ControlState.LeftAileronAngle  = LeftAileronAngleValue;
	ControlState.RightAileronAngle = RightAileronAngleValue;
}

void APhysicalAirplane::UpdateElevatorControl(float LeftElevatorAngleValue, float RightElevatorAngleValue)
{
	ControlState.LeftElevatorAngle  = LeftElevatorAngleValue;
	ControlState.RightElevatorAngle = RightElevatorAngleValue;
}

void APhysicalAirplane::UpdateRudderControl(float RudderAngleValue)
{
	ControlState.RudderAngle = RudderAngleValue;
}

void APhysicalAirplane::GenerateAerodynamicPhysicalConfigutation()
{
	AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(Surfaces);
}

UTexture2D* APhysicalAirplane::GetCameraOutputTexture() const
{
	return CameraComp ? CameraComp->OutputTexture : nullptr;
}
