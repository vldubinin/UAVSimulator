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

	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces) {
		Surface->OnConstruction();
	}
}

// Called when the game starts or when spawned
void APhysicalAirplane::BeginPlay()
{
	Super::BeginPlay();
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);
}

// Called every frame
void APhysicalAirplane::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void APhysicalAirplane::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

