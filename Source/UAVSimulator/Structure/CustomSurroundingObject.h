#pragma once

#include "CoreMinimal.h"
#include "CustomSurroundingObject.generated.h"

/**
 * One object loaded by UCustomSurroundingsScannerComponent from its JSON source: a stable id,
 * a free-form type tag (e.g. "building", "tree"), a single "altitude", and a "bbox" — four
 * geographic corners ("x_min", "x_max", "y_min", "y_max", each a {latitude, longitude} pair)
 * that outline the object's footprint. Latitude/Longitude here are the mean of those four
 * corners (the footprint centre); WorldLocationMeters is that centre converted to world space,
 * and BBoxCornersWorldMeters holds the four corners converted the same way, in winding order
 * x_min -> x_max -> y_min -> y_max. Re-parsed from ObjectsJson whenever that string changes
 * (see LoadObjects()), so every field here tracks live edits to the source JSON without a restart.
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

	/** Mean latitude of the four "bbox" corners, in degrees — the footprint centre. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double Latitude = 0.0;

	/** Mean longitude of the four "bbox" corners, in degrees — the footprint centre. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double Longitude = 0.0;

	/** "altitude" from the source JSON, in metres above the ellipsoid — shared by every corner. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double AltitudeMeters = 0.0;

	/**
	 * Footprint centre in world space, in metres — the mean of BBoxCornersWorldMeters (each of
	 * which is a "bbox" corner run through
	 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal at load time).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	FVector WorldLocationMeters = FVector::ZeroVector;

	/**
	 * The four "bbox" corners in world space, in metres, in winding order
	 * x_min -> x_max -> y_min -> y_max — each converted via
	 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal when the JSON was
	 * (re)loaded, at AltitudeMeters. Drives the debug bbox outline and its projected pixel size.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	TArray<FVector> BBoxCornersWorldMeters;

	/** Distance from the scanning actor to WorldLocationMeters, in metres — refreshed every Scan(). */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	float DistanceMeters = 0.0f;
};
