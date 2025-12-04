// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicalAirplane.h"



// Sets default values
APhysicalAirplane::APhysicalAirplane()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
}

void APhysicalAirplane::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	CalculateParameters();
	ControlState = ControlInputState();
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces) {
		Surface->OnConstruction(CenterOfMassInWorld, ControlSurfaces);
	}
}

// Called when the game starts or when spawned
void APhysicalAirplane::BeginPlay()
{
	Super::BeginPlay();
	ControlState = ControlInputState();
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);

	GetComponents<UControlSurfaceSC>(ControlSurfaces);
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces) {
		Surface->OnConstruction(CenterOfMassInWorld, ControlSurfaces);
	}
}

// Called every frame
void APhysicalAirplane::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UE_LOG(LogTemp, Warning, TEXT("### TICK ###"));
	CalculateParameters(); 

	UStaticMeshComponent* StaticMeshComponent = this->FindComponentByClass<UStaticMeshComponent>();
	if (StaticMeshComponent && StaticMeshComponent->IsSimulatingPhysics())
	{
		DrawDebugDirectionalArrow(
			GetWorld(),
			CenterOfMassInWorld,
			CenterOfMassInWorld + LinearVelocity * 200,
			25.0f, FColor::Red, false, -1.f, 0, 5.0f);

		DrawDebugDirectionalArrow(
			GetWorld(),
			CenterOfMassInWorld,
			CenterOfMassInWorld + AirflowDirection * 200,
			25.0f, FColor::Cyan, false, -1.f, 0, 5.0f);
		
		DrawDebugCrosshairs(GetWorld(), CenterOfMassInWorld, FRotator::ZeroRotator, 250, FColor::Red, false, -1, 0);

		AerodynamicForce TotalAerodynamicForce;
		for (UAerodynamicSurfaceSC* Surface : Surfaces) {
			AerodynamicForce SurfaceAerodynamicForce = Surface->CalculateForcesOnSurface(CenterOfMassInWorld, LinearVelocity, AngularVelocity, AirflowDirection, ControlState);
			TotalAerodynamicForce.PositionalForce += SurfaceAerodynamicForce.PositionalForce;
			TotalAerodynamicForce.RotationalForce += SurfaceAerodynamicForce.RotationalForce;
		}
	
		StaticMeshComponent->AddForce(TotalAerodynamicForce.PositionalForce);
		StaticMeshComponent->AddTorqueInRadians(TotalAerodynamicForce.RotationalForce);

		if (ThrottlePercent > 0.0f) {
			float MaxThrust = 1500000.f;
			FVector ThrustDirection = GetActorForwardVector();
			FVector ThrustForce = ThrustDirection * MaxThrust * ThrottlePercent;
			StaticMeshComponent->AddForce(ThrustForce);
		}

		FVector CurrentLocation = GetActorLocation();

		// 2. Format the log string
		FString LogString = FString::Printf(
			TEXT("%s Location: %s"),
			*GetName(),
			*CurrentLocation.ToString()
		);

		// 3. Log the message to the Unreal Output Log (Ctrl+Shift+Z to view)
		// We use the custom LogPositionLogger category defined in the header.
		UE_LOG(LogTemp, Log, TEXT("%s"), *LogString);

	}
	ControlState = ControlInputState();
}

void  APhysicalAirplane::CalculateParameters()
{
	//Calculate LinearVelocity
	//Calculate AngularVelocity
	//Calculate CenterOfMass
	UStaticMeshComponent* StaticMeshComponent = this->FindComponentByClass<UStaticMeshComponent>();
	if (StaticMeshComponent && StaticMeshComponent->IsSimulatingPhysics())
	{
		LinearVelocity = StaticMeshComponent->GetPhysicsLinearVelocity();
		AngularVelocity = StaticMeshComponent->GetPhysicsAngularVelocityInRadians();
		CenterOfMassInWorld = StaticMeshComponent->GetCenterOfMass();
	}
	else {
		AngularVelocity = FVector();
		CenterOfMassInWorld = FVector();
	}

	//Calculate AirflowDirection
	const FVector ActorVelocity = GetVelocity();
	if (ActorVelocity.IsNearlyZero())
	{
		AirflowDirection = FVector();
	}
	else {
		AirflowDirection = -ActorVelocity.GetSafeNormal();
	}
}

void APhysicalAirplane::UpdateAileronControl(float LeftAileronAngleValue, float RightAileronAngleValue)
{
	ControlState.LeftAileronAngle = LeftAileronAngleValue;
	ControlState.RightAileronAngle = RightAileronAngleValue;
}

void APhysicalAirplane::UpdateElevatorControl(float LeftElevatorAngleValue, float RightElevatorAngleValue)
{
	ControlState.LeftElevatorAngle = LeftElevatorAngleValue;
	ControlState.RightElevatorAngle = RightElevatorAngleValue;
}

void APhysicalAirplane::UpdateRudderControl(float RudderAngleValue)
{
	ControlState.RudderAngle = RudderAngleValue;
}



void APhysicalAirplane::GenerateAerodynamicPhysicalConfigutation(UObject* ContextObject)
{
	AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(ContextObject, Surfaces);
}