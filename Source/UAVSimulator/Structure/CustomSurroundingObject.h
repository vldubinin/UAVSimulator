#pragma once

#include "CoreMinimal.h"
#include "CustomSurroundingObject.generated.h"

/**
 * One object loaded by UCustomSurroundingsScannerComponent from its JSON source: a stable id,
 * a free-form type tag (e.g. "building", "tree"), its geographic position as given in the JSON,
 * and the world-space position that geographic position was converted to via ACesiumGeoreference.
 * Re-parsed from ObjectsJson whenever that string changes (see LoadObjects()), so every field here
 * tracks live edits to the source JSON without requiring a restart.
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
	 * was (re)loaded.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	FVector WorldLocationMeters = FVector::ZeroVector;

	/** Distance from the scanning actor, in metres — refreshed every Scan(). */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	float DistanceMeters = 0.0f;

	/** "bboxw" from the source JSON — width, in metres, of the debug bbox drawn around the point. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	float BBoxWidthMeters = 5.0f;

	/** "bboxh" from the source JSON — height, in metres, of the debug bbox drawn around the point. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	float BBoxHeightMeters = 5.0f;
};
