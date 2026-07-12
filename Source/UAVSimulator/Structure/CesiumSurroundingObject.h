#pragma once

#include "CoreMinimal.h"
#include "CesiumSurroundingObject.generated.h"

/**
 * One feature hit by UCesiumSurroundingsScannerComponent's scan: the actor/component
 * it belongs to, its distance from the scanning actor, and whatever Cesium metadata
 * properties (e.g. Longitude/Latitude/Height, set up per
 * https://cesium.com/learn/unreal/unreal-visualize-metadata) its property table carries.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FCesiumSurroundingObject
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	FString ComponentName;

	/** Distance from the scanning actor, in metres. */
	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	float DistanceMeters = 0.0f;

	/** World-space hit location, in metres (converted from Unreal cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	FVector HitLocationMeters = FVector::ZeroVector;

	/** Property table values for the hit feature, keyed by property name (e.g. "Longitude", "Latitude", "Height"). */
	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	TMap<FString, FString> Metadata;
};
