#pragma once

#include "CoreMinimal.h"
#include "Chord.generated.h"

USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FChord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector StartPoint;

	UPROPERTY(BlueprintReadOnly)
	FVector EndPoint;

	UPROPERTY(BlueprintReadOnly)
	float Length;

	FChord()
		: StartPoint(FVector::ZeroVector)
		, EndPoint(FVector::ZeroVector)
		, Length(0.f)
	{}

	FChord(const FVector InStartPoint, const FVector InEndPoint)
		: StartPoint(InStartPoint)
		, EndPoint(InEndPoint)
		, Length(InEndPoint.X - InStartPoint.X)
	{}
};
