#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Structure/CesiumSurroundingObject.h"
#include "CesiumSurroundingsScannerComponent.generated.h"

class UUAVCameraComponent;
class USceneCaptureComponent2D;
class ACesium3DTileset;
class UPrimitiveComponent;

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
 * the same feature are merged into a single entry per scan.
 *
 * Merged entries are then reconciled against ObjectStorage, a persistent map of currently-
 * tracked features:
 *   1. A feature already in storage is left untouched while it keeps getting hit — its data
 *      is frozen to whatever it was first seen with, even if a later scan's hit differs slightly.
 *   2. A feature that this scan's sweep didn't hit (e.g. a momentary gap between rays, or the
 *      feature occluded behind something else) is NOT dropped immediately: its frozen world
 *      position is re-projected onto the camera, and it is kept in storage — still "reproduced" —
 *      for as long as that projection lands inside the camera's screen bounds. Only once its
 *      frozen position would actually fall outside the frame (or behind the camera) is it removed.
 *   3. ObjectStorage is only ever mutated through AddObject() (a newly-seen feature) and
 *      RemoveObject() (a feature whose frozen position has left the frame).
 *   4. Everything downstream — the console log and the debug rays (one ray per tracked
 *      feature, redrawn every tick from the airplane's current position, same picking
 *      technique as https://cesium.com/learn/unreal/unreal-visualize-metadata/) — is driven
 *      from ObjectStorage, not from the fresh per-scan sweep result.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCesiumSurroundingsScannerComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UCesiumSurroundingsScannerComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("cesium_objects"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

public:
	/**
	 * Runs a scan immediately on the game thread, reconciles ObjectStorage against what's
	 * currently visible (adding newly-seen features, removing ones no longer in view), updates
	 * LatestScanResults from ObjectStorage, and returns a reference to it. Called automatically
	 * by TickComponent at ScanRate Hz; can also be triggered directly from Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cesium Surroundings")
	const TArray<FCesiumSurroundingObject>& Scan();

	/** Mirrors ObjectStorage — every feature currently visible, each with its frozen first-seen data. */
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

	/**
	 * After this many calls to Scan(), the lower half of the vertical FOV (VAngleRad < 0,
	 * i.e. everything below the camera's forward axis) is permanently excluded from the
	 * sweep grid — only the upper half keeps being scanned. 0 disables the cutoff, so the
	 * full vertical FOV is always scanned.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 0))
	int32 FramesBeforeLowerHalfCutoff = 10;

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

	/** Color of the debug ray drawn from the airplane's current position to each scanned feature. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug")
	FColor RayDebugColor = FColor::Yellow;

	/**
	 * Draws a wireframe of the current sweep area every scan: four edges from the origin out to
	 * the far corners at ScanRadiusMeters, plus the far rectangle connecting them. Reflects
	 * FramesBeforeLowerHalfCutoff live — once the cutoff kicks in, the bottom edge sits on the
	 * camera's forward axis instead of the bottom of the FOV, so the wireframe visibly shrinks
	 * to the upper half.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug")
	bool bDrawScanArea = true;

	/** Color of the scan-area wireframe (see bDrawScanArea). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug")
	FColor ScanAreaDebugColor = FColor::Cyan;

	// ── Sensor output (IUAVSensorInterface) ────────────────────────────────────
	// Property table key names read as lat/long/altitude. Configurable because different
	// tilesets name these properties differently (e.g. "lat"/"lon" vs "Latitude"/"Longitude").

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Sensor")
	FString ObjectPropertyName = TEXT("elementId");

	/** Property table key read as the object's latitude (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Sensor")
	FString LatitudePropertyName = TEXT("cesium#latitude");

	/** Property table key read as the object's longitude (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Sensor")
	FString LongitudePropertyName = TEXT("cesium#longitude");

	/** Property table key read as the object's altitude/height (metres). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Sensor")
	FString AltitudePropertyName = TEXT("Height");

private:
	/**
	 * Sweeps a sphere (SweepRadius) along a HorizontalRays x VerticalLayers grid of directions
	 * spanning exactly CameraComponent's HorizontalFOVDeg x VerticalFOVDeg, out to
	 * ScanRadiusMeters, from OriginTransform (the camera's own transform — see Scan()). Each
	 * direction is built so its horizontal/vertical angle relative to OriginTransform's forward
	 * axis lands exactly inside [-HalfFOV, +HalfFOV] — the same rectangular-frustum test a
	 * perspective camera uses — so nothing outside the camera's view is ever swept. Before
	 * sweeping, a broad-phase (GatherNearbyTileComponents/BuildActiveCellSet) narrows the grid
	 * down to only the cells that could plausibly hit an already-loaded Cesium tile, so cells
	 * pointing at empty sky/ground never pay for a physics query at all. Returns every hit from
	 * every sweep (a single direction can return multiple overlapping hits, same as
	 * SweepMultiByChannel normally does).
	 */
	TArray<FHitResult> SweepScan(const FTransform& OriginTransform, AActor* ActorToIgnore) const;

	/**
	 * Draws the bDrawScanArea wireframe (see its comment): four edges from Origin to the far
	 * corners at Range, plus the far rectangle connecting them. VMinRad/VMaxRad are the
	 * effective vertical bounds for this scan — VMinRad is 0 instead of -HalfVFovRad once
	 * FramesBeforeLowerHalfCutoff has kicked in, so the wireframe shrinks to match exactly what
	 * SweepScan's grid is currently covering.
	 */
	void DrawScanAreaDebug(const FTransform& OriginTransform, float Range, float HalfHFovRad, float VMinRad, float VMaxRad) const;

	/**
	 * Broad-phase candidate gathering, no physics involved: every currently-loaded tile
	 * primitive under Tileset whose bounding sphere is within RangeCm of Origin. Cesium already
	 * frustum-culls which tiles are loaded/streamed, so this is just a cheap bounds check over
	 * whatever tile components already exist as children of the tileset actor.
	 */
	TArray<UPrimitiveComponent*> GatherNearbyTileComponents(const FVector& Origin, float RangeCm) const;

	/**
	 * Projects each candidate's bounding sphere into OriginTransform's H/V angle space (the same
	 * angle convention SweepScan's grid uses) and marks every grid cell whose direction falls
	 * within the candidate's angular footprint. SweepScan only sweeps cells in the returned set.
	 * A candidate straddling or behind the camera plane conservatively activates the whole grid
	 * rather than risk silently dropping it.
	 */
	TSet<int32> BuildActiveCellSet(const FTransform& OriginTransform, const TArray<UPrimitiveComponent*>& Candidates,
		float HalfHFovRad, float HalfVFovRad) const;

	/**
	 * Multiple sweep directions often land on the same physical feature (e.g. several points
	 * on the same building's facade) since the property table only distinguishes features, not
	 * individual triangles. Collapses entries that share the same actor/component and identical
	 * metadata into a single entry, averaging their hit locations and keeping the closest
	 * distance, so each building produces one storage entry instead of several.
	 */
	static TArray<FCesiumSurroundingObject> MergeDuplicateHits(const TArray<FCesiumSurroundingObject>& RawEntries);

	/** Stable identity for a feature — same actor/component + identical metadata — used as its ObjectStorage key. */
	static FString BuildFeatureKey(const FCesiumSurroundingObject& Entry);

	/** ObjectStorage operation 1/2: registers a newly-seen feature and logs its discovery. */
	void AddObject(const FString& Key, const FCesiumSurroundingObject& Entry);

	/** ObjectStorage operation 2/2: forgets a feature once its frozen position has left the frame. */
	void RemoveObject(const FString& Key);

	/**
	 * Builds and caches this tick's IUAVSensorInterface JSON payload from ObjectStorage —
	 * one object per currently-visible feature, each with a stable id, latitude/longitude/
	 * altitude parsed out of Metadata (via LatitudePropertyName/LongitudePropertyName/
	 * AltitudePropertyName), and pixel_x/pixel_y/visible from ProjectWorldToScreen. Called at
	 * the end of TickComponent, after Scan(); does nothing (and drops the cached frame) while
	 * bSensorEnabled is false.
	 */
	void BuildSensorFrame();

	/**
	 * Lazily reads SensorSizeX/SensorSizeY from SceneCaptureComponent's TextureTarget. Split out
	 * from BuildSensorFrame() (which only runs while bSensorEnabled is true) so ProjectWorldToScreen
	 * has valid sensor dimensions to test Scan()'s "still in frame?" retention check against
	 * regardless of whether ZMQ sensor publishing is enabled. Called unconditionally at the top
	 * of TickComponent, before Scan().
	 */
	void UpdateSensorSize();

	/**
	 * Projects a single world-space point (Unreal cm) onto SceneCaptureComponent's render
	 * target. Identical view/projection matrix setup to
	 * UKeyPointDetectionComponent::ProjectWorldToScreen — see that implementation for the
	 * behind-camera (W<=0) and in-bounds checks. Returns false (point not visible) if the
	 * capture's render target size isn't known yet.
	 *
	 * Doubles as Scan()'s "is this stored feature still in frame?" test: a frozen
	 * ObjectStorage entry that this scan's sweep didn't hit is only removed once its
	 * HitLocationMeters projects outside the returned bounds (or behind the camera).
	 */
	bool ProjectWorldToScreen(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const;

	/** Owner's onboard camera — supplies HorizontalFOVDeg/VerticalFOVDeg for the scan grid. */
	UPROPERTY()
	UUAVCameraComponent* CameraComponent = nullptr;

	/** Owner's scene capture — supplies the transform (position + orientation) the scan is swept from. */
	UPROPERTY()
	USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	/**
	 * First Cesium3DTileset found in the world — supplies the loaded-tile components used for
	 * SweepScan's broad-phase culling. Null is a safe fallback: SweepScan just sweeps the full
	 * grid as it always did.
	 */
	UPROPERTY()
	ACesium3DTileset* Tileset = nullptr;

	/**
	 * Persistent storage of currently-tracked features, keyed by BuildFeatureKey(). A feature
	 * stays here — with its data frozen — even across scans that fail to hit it again, as long
	 * as its frozen position still projects inside the camera's frame (see ProjectWorldToScreen).
	 * Only ever mutated via AddObject()/RemoveObject() — see class docs. LatestScanResults, the
	 * console log, and the debug rays are all driven from this, not from the raw per-scan sweep result.
	 */
	TMap<FString, FCesiumSurroundingObject> ObjectStorage;

	/** Number of completed Scan() calls so far; compared against FramesBeforeLowerHalfCutoff. */
	int32 ScanCount = 0;

	/** SceneCaptureComponent's render-target resolution — lazily read in BuildSensorFrame() since UAVCameraComponent assigns TextureTarget in its own BeginPlay. */
	int32 SensorSizeX = 0;
	int32 SensorSizeY = 0;

	// Latest serialized IUAVSensorInterface frame — written and read on the game thread only.
	TArray<uint8> LatestPayload;
	double        LatestTimestamp = 0.0;
	bool          bHasFrame       = false;
};
