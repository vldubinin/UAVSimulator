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

	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces) {
		Surface->OnConstruction(CenterOfMassInWorld);
	}
}

// Called when the game starts or when spawned
void APhysicalAirplane::BeginPlay()
{
	Super::BeginPlay();
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces) {
		Surface->OnConstruction(CenterOfMassInWorld);
	}
}

// Called every frame
void APhysicalAirplane::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	CalculateParameters(); 

	UStaticMeshComponent* StaticMeshComponent = this->FindComponentByClass<UStaticMeshComponent>();
	if (StaticMeshComponent && StaticMeshComponent->IsSimulatingPhysics())
	{
		/*DrawDebugDirectionalArrow(
			GetWorld(),
			CenterOfMassInWorld,
			CenterOfMassInWorld + LinearVelocity * 200,
			25.0f, FColor::Red, false, -1.f, 0, 5.0f);

		DrawDebugDirectionalArrow(
			GetWorld(),
			CenterOfMassInWorld,
			CenterOfMassInWorld + AirflowDirection * 200,
			25.0f, FColor::Cyan, false, -1.f, 0, 5.0f);
		*/
		DrawDebugCrosshairs(GetWorld(), CenterOfMassInWorld, FRotator::ZeroRotator, 250, FColor::Red, false, -1, 0);

		AerodynamicForce TotalAerodynamicForce;
		for (UAerodynamicSurfaceSC* Surface : Surfaces) {
			AerodynamicForce SurfaceAerodynamicForce = Surface->CalculateForcesOnSurface(CenterOfMassInWorld, LinearVelocity, AngularVelocity, AirflowDirection);
			TotalAerodynamicForce.PositionalForce += SurfaceAerodynamicForce.PositionalForce;
			TotalAerodynamicForce.RotationalForce += SurfaceAerodynamicForce.RotationalForce;
		}
		UE_LOG(LogTemp, Warning, TEXT("Surface Positional Force: %s"), *TotalAerodynamicForce.PositionalForce.ToString());
		UE_LOG(LogTemp, Warning, TEXT("Surface Rotational Force: %s"), *TotalAerodynamicForce.RotationalForce.ToString());
		StaticMeshComponent->AddForce(TotalAerodynamicForce.PositionalForce);
		StaticMeshComponent->AddTorqueInRadians(TotalAerodynamicForce.RotationalForce);

		if (ThrottlePercent > 0.0f) {
			float MaxThrust = 5000000.f;
			FVector ThrustDirection = GetActorForwardVector();
			FVector ThrustForce = ThrustDirection * MaxThrust * ThrottlePercent;
			StaticMeshComponent->AddForce(ThrustForce);
		}
	}
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

// Called to bind functionality to input
void APhysicalAirplane::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAxis("Thrust", this, &APhysicalAirplane::UpdateThrottle);
}

void APhysicalAirplane::UpdateThrottle(float Value)
{
	float DeltaTime = GetWorld()->GetDeltaSeconds();
	float NewThrottle = ThrottlePercent + (Value * DeltaTime);
	ThrottlePercent = FMath::Clamp(NewThrottle, 0.0f, 1.0f);
}