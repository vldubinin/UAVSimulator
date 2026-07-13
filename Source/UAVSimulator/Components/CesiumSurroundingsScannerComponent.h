#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "UAVSimulator/Structure/CesiumSurroundingObject.h"
#include "CesiumSurroundingsScannerComponent.generated.h"

class UUAVCameraComponent;
class USceneCaptureComponent2D;

/**
 * Sweeps a small sphere (SweepMultiByChannel) along a grid of directions spanning exactly
 * the owning actor's onboard camera (UUAVCameraComponent/USceneCaptureComponent2D)
 * horizontal/vertical field of view, from the camera's own transform — so the scan finds
 * every Cesium object the camera can see and nothing outside its view. Each ray direction
 * sweeps a thick sphere (SweepRadius) rather than a zero-width line trace, so gaps between
 * angularly-adjacent rays don't let objects slip through at long range. For every hit that
 * lands on a Cesium 3D Tiles feature carrying a property table — set up on the tileset's
 * CesiumFeaturesMetadataComponent per
 * https://cesium.com/learn/unreal/unreal-visualize-metadata — reads that feature's metadata
 * (e.g. Longitude/Latitude/Height) via
 * UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit. Hits that land on
 * the same feature are merged into a single entry, logged to the console, and a debug ray
 * is drawn from the owner to each merged feature — the same line-trace-with-"Draw Debug
 * Type: For Duration" visualization used to pick a feature at
 * https://cesium.com/learn/unreal/unreal-visualize-metadata/.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCesiumSurroundingsScannerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCesiumSurroundingsScannerComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

public:
	/**
	 * Runs a scan immediately on the game thread, updates LatestScanResults, prints the
	 * metadata of every merged feature to the console, and returns a reference to
	 * LatestScanResults. Called automatically by TickComponent at ScanRate Hz; can also
	 * be triggered directly from Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cesium Surroundings")
	const TArray<FCesiumSurroundingObject>& Scan();

	/** Most recent scan results (hits on the same feature already merged into one entry). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cesium Surroundings")
	TArray<FCesiumSurroundingObject> LatestScanResults;

	// ── Scan parameters ───────────────────────────────────────────────────────

	/** Maximum detection range, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1.0f))
	float ScanRadiusMeters = 10000.0f;

	/** Number of rays distributed evenly across the camera's horizontal FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1))
	int32 HorizontalRays = 72;

	/** Number of rays distributed evenly across the camera's vertical FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1))
	int32 VerticalLayers = 8;

	/** How many full scans are performed per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 0.01f, ClampMax = 100.0f))
	float ScanRate = 1.0f;

	/** Collision channel used for the sweeps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

	/**
	 * Radius (cm) of the sphere swept along each ray direction. Gives each ray thickness so
	 * objects between two angularly-adjacent rays aren't missed at long range — raise this if
	 * a full lidar-style sweep still leaves gaps, lower it to reduce duplicate/overlapping hits.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1.0f))
	float SweepRadius = 100.0f;

	/**
	 * Which feature ID set to read on a hit primitive (index into the primitive's
	 * CesiumPrimitiveFeatures). Matches the "Feature ID Set Index" used by
	 * "Get Property Table Values From Hit" in Blueprint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 0))
	int64 FeatureIDSetIndex = 0;

	// ── Debug rays ─────────────────────────────────────────────────────────────

	/** Color of the debug ray drawn from the owner to each scanned feature. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug")
	FColor RayDebugColor = FColor::Yellow;

	/** How long (s) each ray stays visible — mirrors the tutorial's "Draw Debug Type: For Duration". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug", meta = (ClampMin = 0.0f))
	float RayDebugDuration = 1.0f;

private:
	/**
	 * Sweeps a sphere (SweepRadius) along a HorizontalRays x VerticalLayers grid of directions
	 * spanning exactly CameraComponent's HorizontalFOVDeg x VerticalFOVDeg, out to
	 * ScanRadiusMeters, from OriginTransform (the camera's own transform — see Scan()). Each
	 * direction is built so its horizontal/vertical angle relative to OriginTransform's forward
	 * axis lands exactly inside [-HalfFOV, +HalfFOV] — the same rectangular-frustum test a
	 * perspective camera uses — so nothing outside the camera's view is ever swept. Returns
	 * every hit from every sweep (a single direction can return multiple overlapping hits, same
	 * as SweepMultiByChannel normally does).
	 */
	TArray<FHitResult> SweepScan(const FTransform& OriginTransform, AActor* ActorToIgnore) const;

	/**
	 * Multiple sweep directions often land on the same physical feature (e.g. several points
	 * on the same building's facade) since the property table only distinguishes features, not
	 * individual triangles. Collapses entries that share the same actor/component and identical
	 * metadata into a single entry, averaging their hit locations and keeping the closest
	 * distance, so each building produces one log line/ray instead of several.
	 */
	static TArray<FCesiumSurroundingObject> MergeDuplicateHits(const TArray<FCesiumSurroundingObject>& RawEntries);

	/** Owner's onboard camera — supplies HorizontalFOVDeg/VerticalFOVDeg for the scan grid. */
	UPROPERTY()
	UUAVCameraComponent* CameraComponent = nullptr;

	/** Owner's scene capture — supplies the transform (position + orientation) the scan is swept from. */
	UPROPERTY()
	USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	float ScanAccumulator = 0.0f;
};
