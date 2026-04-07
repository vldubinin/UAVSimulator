#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/Chord.h"

class USceneComponent;

/**
 * Helpers for converting local-space positions and chords to world space via a USceneComponent's transform.
 */
class UAVSIMULATOR_API CoordinateTransformUtil
{
public:
	static TArray<FVector> LocalToWorld(USceneComponent* Component, const TArray<FVector>& LocalPositions);
	static FVector         LocalToWorld(USceneComponent* Component, FVector LocalPosition);
	static FChord          ChordLocalToWorld(USceneComponent* Component, FChord LocalChord);
};
