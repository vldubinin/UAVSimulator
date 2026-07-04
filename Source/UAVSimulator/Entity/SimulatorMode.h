#pragma once

#include "CoreMinimal.h"
#include "SimulatorMode.generated.h"

UENUM(BlueprintType)
enum class ESimulatorMode : uint8
{
	RecordTarget         UMETA(DisplayName = "Record Target"),
	PlaybackAndTrack     UMETA(DisplayName = "Playback and Track"),
	PlaybackAndAutoTrack UMETA(DisplayName = "Playback and Auto Track"),
	Playback             UMETA(DisplayName = "Playback"),
	Free                 UMETA(DisplayName = "Free"),
};
