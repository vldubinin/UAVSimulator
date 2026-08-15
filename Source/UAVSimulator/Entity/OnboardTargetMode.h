#pragma once

#include "CoreMinimal.h"
#include "OnboardTargetMode.generated.h"

/** Which airplane role the onboard camera / sensor bus should be active on, independent of ESimulatorMode. */
UENUM(BlueprintType)
enum class EOnboardTargetMode : uint8
{
	Drone  UMETA(DisplayName = "Drone"),
	Target UMETA(DisplayName = "Target"),
	None   UMETA(DisplayName = "None"),
};
