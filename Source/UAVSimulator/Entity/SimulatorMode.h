#pragma once

#include "CoreMinimal.h"
#include "SimulatorMode.generated.h"

UENUM(BlueprintType)
enum class ESimulatorMode : uint8
{
	RecordTarget     UMETA(DisplayName = "Record Target"),
	PlaybackAndTrack UMETA(DisplayName = "Playback and Track"),
	Playback         UMETA(DisplayName = "Playback"),
};
