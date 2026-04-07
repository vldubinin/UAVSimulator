#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/Chord.h"

class USceneComponent;

/**
 * Static helpers for visualising aerodynamic surfaces and forces in the editor and PIE.
 * All functions are no-ops in shipping builds.
 */
class UAVSIMULATOR_API AerodynamicDebugRenderer
{
public:
	/** Draw start and end airfoil profiles plus leading/trailing edge borders. */
	static void DrawSurface(USceneComponent* Parent,
		FChord StartChord, FChord EndChord,
		const TArray<FVector>& StartProfile, const TArray<FVector>& EndProfile,
		FName Name);

	/** Draw a spline through the given local-space points attached to Parent. */
	static void DrawSpline(USceneComponent* Parent, const TArray<FVector>& Points, FName Name);

	/** Draw the flap hinge line if flap angles are non-zero. */
	static void DrawFlap(USceneComponent* Parent,
		FChord StartChord, FChord EndChord,
		float StartFlapPosition, float EndFlapPosition,
		float MinFlapAngle, float MaxFlapAngle,
		FName Name);

	/** Draw a small crosshairs marker at the given local-space point. */
	static void DrawCrosshairs(USceneComponent* Parent, FVector Point, FName Name);

	/** Attach a TextRenderComponent label near the given local-space point. */
	static void DrawLabel(USceneComponent* Parent,
		FString Text, FVector Point, FVector Offset,
		FRotator Rotator, FColor Color, FName Name);

	/** Draw a world-space directional arrow representing a force vector. */
	static void DrawForceArrow(const UWorld* World,
		FVector Origin, FVector Direction,
		FColor Color, float Scale,
		float ArrowSize, float Thickness, float Duration);

private:
	static FVector GetPointOnLineAtPercentage(FVector Start, FVector End, float Alpha);
};
