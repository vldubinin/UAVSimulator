#pragma once

#include "CoreMinimal.h"
#include "OnboardTargetMode.generated.h"

/** Для якої ролі літака мають бути активні бортова камера / сенсорна шина, незалежно від ESimulatorMode. */
UENUM(BlueprintType)
enum class EOnboardTargetMode : uint8
{
	Drone  UMETA(DisplayName = "Drone"),
	Target UMETA(DisplayName = "Target"),
	None   UMETA(DisplayName = "None"),
};
