// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Components/SplineComponent.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Util/AerodynamicPhysicalCalculationUtil.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"

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


protected:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Generate Aerodynamic Physical Configutation"), Category = "Editor Tools")
		void GenerateAerodynamicPhysicalConfigutation(UObject* ContextObject);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Unreal", meta = (ToolTip = "Швидкість роботи симуляції. Значення '1' відповідає звичайній швидкості."))
		float DebugSimulatorSpeed = 1.0f;


private:
	void CalculateParameters();

private:
	void UpdateThrottle(float Value);

private:
	TArray<UAerodynamicSurfaceSC*> Surfaces;
	FVector LinearVelocity;
	FVector AngularVelocity;
	FVector CenterOfMassInWorld;
	FVector AirflowDirection;

	float ThrottlePercent = 1.f;
};
