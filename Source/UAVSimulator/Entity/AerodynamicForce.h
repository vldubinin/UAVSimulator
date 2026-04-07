#pragma once

#include "CoreMinimal.h"
#include "AerodynamicForce.generated.h"

USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FAerodynamicForce
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector PositionalForce;

	UPROPERTY(BlueprintReadOnly)
	FVector RotationalForce;

	FAerodynamicForce()
		: PositionalForce(FVector::ZeroVector)
		, RotationalForce(FVector::ZeroVector)
	{}

	FAerodynamicForce(const FVector LiftForce, const FVector DragForce, const FVector TorqueForce, FVector RelativePosition)
		: PositionalForce(LiftForce + DragForce)
		, RotationalForce(TorqueForce + FVector::CrossProduct(RelativePosition, LiftForce + DragForce))
	{}
};

// Compatibility alias — remove once all call sites use FAerodynamicForce
using AerodynamicForce = FAerodynamicForce;
