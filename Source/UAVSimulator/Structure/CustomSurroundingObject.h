#pragma once

#include "CoreMinimal.h"
#include "CustomSurroundingObject.generated.h"

/**
 * One object loaded by UCustomSurroundingsScannerComponent from its JSON source: a stable id,
 * a free-form type tag (e.g. "building", "tree"), its geographic position as given in the JSON,
 * and the world-space position that geographic position was converted to (once, at load time)
 * via ACesiumGeoreference.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FCustomSurroundingObject
{
	GENERATED_BODY()

	/** "elementId" from the source JSON — stable identity, used as the ObjectStorage key. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	FString ObjectID;

	/** "type" from the source JSON (e.g. "building", "tree"). */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	FString ObjectType;

	/** "latitude" from the source JSON, in degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double Latitude = 0.0;

	/** "longitude" from the source JSON, in degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double Longitude = 0.0;

	/** "altitude" from the source JSON, in metres above the ellipsoid. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double AltitudeMeters = 0.0;

	/**
	 * World-space position, in metres, that Latitude/Longitude/AltitudeMeters were converted to
	 * via ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal when the JSON
	 * was loaded. Cached once — never recomputed per-scan.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	FVector WorldLocationMeters = FVector::ZeroVector;

	/** Distance from the scanning actor, in metres — refreshed every Scan(). */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	float DistanceMeters = 0.0f;
};
