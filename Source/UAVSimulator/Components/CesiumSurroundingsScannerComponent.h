#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Structure/CesiumSurroundingObject.h"
#include "CesiumSurroundingsScannerComponent.generated.h"

class UUAVCameraComponent;
class USceneCaptureComponent2D;

/**
 * Sweeps a small sphere (SweepMultiByChannel) along a downward-facing cone of directions
 * centered on the owning actor's nadir (straight down) — same angular pattern as
 * ULidarComponent, but a thick swept sphere per direction instead of a zero-width line
 * trace, so gaps between the radially-diverging rays no longer let objects slip through at
 * long range. For every hit that lands on a Cesium 3D
 * Tiles feature carrying a property table — set up on the tileset's
 * CesiumFeaturesMetadataComponent per https://cesium.com/learn/unreal/unreal-visualize-metadata —
 * reads that feature's metadata (e.g. Longitude/Latitude/Height) via
 * UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit and prints it to
 * the console. Also draws a debug ray from the owner to each scanned feature — the same
 * line-trace-with-"Draw Debug Type: For Duration" visualization used to pick a feature at
 * https://cesium.com/learn/unreal/unreal-visualize-metadata/.
 *
 * Implements IUAVSensorInterface — SensorBusComponent auto-discovers this component and
 * calls GetLatestFrame() each bus tick to retrieve the scan results as a JSON-encoded
 * payload on the "cesium_surroundings" topic.
 *
 * Every tick (independent of ScanRate), it also cheaply tests each cached scan result's
 * hit location against the owner's UUAVCameraComponent FOV cone and prints the metadata
 * of whichever ones currently fall inside the camera's view to the console. The
 * (comparatively expensive) ray sweep that builds the candidate list still only runs at
 * ScanRate Hz — real-world Cesium features are static, so re-testing already-known
 * positions against the moving camera each frame is enough to know what's in frame now.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCesiumSurroundingsScannerComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UCesiumSurroundingsScannerComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("cesium_surroundings"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/**
	 * Runs a scan immediately on the game thread, updates LatestScanResults, prints the
	 * metadata of every hit feature to the console, and returns a reference to
	 * LatestScanResults. Called automatically by TickComponent at ScanRate Hz; can also
	 * be triggered directly from Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cesium Surroundings")
	const TArray<FCesiumSurroundingObject>& Scan();

	/** Most recent scan results. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cesium Surroundings")
	TArray<FCesiumSurroundingObject> LatestScanResults;

	// ── Scan parameters ───────────────────────────────────────────────────────

	/** Maximum detection range, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1.0f))
	float ScanRadiusMeters = 10000.0f;

	/** Number of rays distributed evenly over a full 360° horizontal sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1))
	int32 HorizontalRays = 72;

	/** Number of evenly-spaced vertical scan layers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1))
	int32 VerticalLayers = 8;

	/** Total angle (deg) of the downward-facing scan cone, centered on straight-down (nadir). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1.0f, ClampMax = 180.0f))
	float VerticalFOVDeg = 90.0f;

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

	// ── Markers ────────────────────────────────────────────────────────────────

	/** Spawn a marker actor at every scanned object's world position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Markers")
	bool bSpawnMarkers = true;

	/** Actor class to spawn as a marker. If unset, a small default sphere marker is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Markers")
	TSubclassOf<AActor> MarkerClass;

	/** Uniform scale applied to the default sphere marker (ignored when MarkerClass is set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Markers", meta = (ClampMin = 0.01f))
	float MarkerScale = 1.0f;

	// ── Debug rays ─────────────────────────────────────────────────────────────

	/** Color of the debug ray drawn from the owner to each scanned feature. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug")
	FColor RayDebugColor = FColor::Yellow;

	/** How long (s) each ray stays visible — mirrors the tutorial's "Draw Debug Type: For Duration". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug", meta = (ClampMin = 0.0f))
	float RayDebugDuration = 1.0f;

private:
	/**
	 * Sweeps a sphere (SweepRadius) along each direction of the HorizontalRays x VerticalLayers
	 * angular fan, out to ScanRadiusMeters. The vertical layers span VerticalFOVDeg centered on
	 * nadir (straight down), so the whole fan forms a downward-facing cone rather than a band
	 * around the horizon. Returns every hit from every sweep (a single direction can return
	 * multiple overlapping hits, same as SweepMultiByChannel normally does).
	 */
	TArray<FHitResult> SweepScan(const FTransform& OriginTransform, AActor* ActorToIgnore) const;

	FString SerializeResults(const TArray<FCesiumSurroundingObject>& Results) const;

	/** Tests LatestScanResults against the camera's FOV cone and logs whichever hits currently fall inside it. */
	void LogBuildingsInCameraFrame() const;

	/** Destroys all actors spawned by a previous scan. */
	void ClearMarkers();

	/** Spawns MarkerClass (or a default sphere) at the given world location and tracks it for later cleanup. */
	void SpawnMarkerAt(const FVector& WorldLocation);

	UPROPERTY()
	UUAVCameraComponent* CameraComponent = nullptr;

	UPROPERTY()
	USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	UPROPERTY()
	TArray<AActor*> SpawnedMarkers;

	float  ScanAccumulator     = 0.0f;
	double LatestScanTimestamp = 0.0; // world time when Scan() last completed
};
