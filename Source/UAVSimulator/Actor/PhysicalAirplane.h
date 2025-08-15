// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "PhysicalAirplane.generated.h"

UCLASS()
class UAVSIMULATOR_API APhysicalAirplane : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	APhysicalAirplane();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void OnConstruction(const FTransform& Transform) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
	TArray<UAerodynamicSurfaceSC*> Surfaces;
	
};
