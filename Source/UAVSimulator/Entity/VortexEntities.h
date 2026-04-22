#pragma once

#include "CoreMinimal.h"
#include "VortexEntities.generated.h"

/**
 * Bound vortex segment positioned at the 1/4-chord line of a wing panel.
 * Represents the circulation-carrying filament in LLT/VLM.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FBoundVortex
{
	GENERATED_BODY()

	/** World-space start point of the vortex filament (e.g. wing root end). */
	UPROPERTY(BlueprintReadOnly)
	FVector StartPoint;

	/** World-space end point of the vortex filament (e.g. wing tip end). */
	UPROPERTY(BlueprintReadOnly)
	FVector EndPoint;

	/** Circulation strength Γ (m²/s). Positive = counter-clockwise when viewed from tip to root. */
	UPROPERTY(BlueprintReadOnly)
	float Gamma;

	FBoundVortex()
		: StartPoint(FVector::ZeroVector)
		, EndPoint(FVector::ZeroVector)
		, Gamma(0.0f)
	{}

	FBoundVortex(FVector InStart, FVector InEnd, float InGamma)
		: StartPoint(InStart)
		, EndPoint(InEnd)
		, Gamma(InGamma)
	{}
};

/**
 * A single node in a trailing vortex wake line shed from a wing panel edge.
 * Nodes are stored sequentially per wake line; consecutive nodes define vortex segments.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FTrailingVortexNode
{
	GENERATED_BODY()

	/** World-space position of this wake node. */
	UPROPERTY(BlueprintReadOnly)
	FVector Position;

	/** Circulation Γ (m²/s) carried by the trailing filament leaving the wing at this node's creation. */
	UPROPERTY(BlueprintReadOnly)
	float Gamma;

	FTrailingVortexNode()
		: Position(FVector::ZeroVector)
		, Gamma(0.0f)
	{}

	FTrailingVortexNode(FVector InPosition, float InGamma)
		: Position(InPosition)
		, Gamma(InGamma)
	{}
};
