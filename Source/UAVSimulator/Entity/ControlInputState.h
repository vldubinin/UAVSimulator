#pragma once

#include "CoreMinimal.h"
#include "ControlInputState.generated.h"

USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FControlInputState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	float LeftAileronAngle;

	UPROPERTY(BlueprintReadWrite)
	float RightAileronAngle;

	UPROPERTY(BlueprintReadWrite)
	float LeftElevatorAngle;

	UPROPERTY(BlueprintReadWrite)
	float RightElevatorAngle;

	UPROPERTY(BlueprintReadWrite)
	float RudderAngle;

	FControlInputState()
		: LeftAileronAngle(0.f)
		, RightAileronAngle(0.f)
		, LeftElevatorAngle(0.f)
		, RightElevatorAngle(0.f)
		, RudderAngle(0.f)
	{}
};

// Псевдонім для сумісності — прибрати, коли всі виклики перейдуть на FControlInputState
using ControlInputState = FControlInputState;
