#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Structure/CustomSurroundingObject.h"
#include "CustomSurroundingsScannerComponent.generated.h"

class USceneCaptureComponent2D;
class ACesiumGeoreference;

/**
 * Same role and shape as UCesiumSurroundingsScannerComponent, but the surrounding objects come
 * from a fixed JSON list (ObjectsJson — hardcoded for now, a placeholder for a future file/
 * network-backed source) instead of a Cesium 3D Tiles sweep. Each JSON entry looks like:
 *
 *   { "elementId": "obj1", "type": "building", "latitude": 50.5656742, "longitude": 31.1472513,
 *     "altitude": 350 }
 *
 * ObjectsJson is parsed once in BeginPlay (LoadObjects()): every entry's Latitude/Longitude/
 * AltitudeMeters is converted, once, to a world-space position via
 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal — see AllObjects.
 *
 * Every Scan() re-tests each loaded object against the camera's current view: within range
 * (ScanRadiusMeters) and projecting inside the camera's frame (ProjectWorldToScreen — identical
 * view/projection math to UCesiumSurroundingsScannerComponent's). There is no physics sweep and
 * no occlusion test — these are known, exact points, not hits swept off a mesh, so a direct
 * per-tick test is authoritative on its own.
 *
 * Objects that currently pass are reconciled against ObjectStorage, a persistent map of
 * currently-visible objects, the same way UCesiumSurroundingsScannerComponent reconciles its
 * ObjectStorage:
 *   1. An object that just started passing is added via AddObject() (and logged).
 *   2. An object that stops passing is removed via RemoveObject().
 *   3. ObjectStorage is only ever mutated through AddObject()/RemoveObject().
 *   4. LatestScanResults, the debug rays, and the sensor JSON payload are all driven from
 *      ObjectStorage, not from AllObjects directly.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCustomSurroundingsScannerComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UCustomSurroundingsScannerComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("custom_objects"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

public:
	/**
	 * Re-tests every object in AllObjects against the camera's current range/frame, reconciles
	 * ObjectStorage against the result (adding newly-visible objects, dropping ones that left
	 * the frame or range), updates LatestScanResults from ObjectStorage, and returns a reference
	 * to it. Called automatically by TickComponent; can also be triggered directly from Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom Surroundings")
	const TArray<FCustomSurroundingObject>& Scan();

	/** Mirrors ObjectStorage — every loaded object currently in view. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom Surroundings")
	TArray<FCustomSurroundingObject> LatestScanResults;

	/**
	 * Source JSON — an array of {elementId, type, latitude, longitude, altitude} objects, e.g.:
	 * [{"elementId":"obj1","type":"building","latitude":50.5656742,"longitude":31.1472513,"altitude":350}]
	 * Hardcoded as an editable default for now; a placeholder for a future file/network-backed
	 * source. Parsed once in BeginPlay via LoadObjects() — editing it at runtime has no effect
	 * until the component is re-initialized.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings", meta = (MultiLine = true))
	FString ObjectsJson;

	// ── Scan parameters ───────────────────────────────────────────────────────

	/** Maximum detection range, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings", meta = (ClampMin = 1.0f))
	float ScanRadiusMeters = 10000.0f;

	// ── Debug markers ──────────────────────────────────────────────────────────
	// Drawn only while bSensorEnabled is true, so they switch off together with the component
	// (same as the sensor JSON payload in BuildSensorFrame) instead of always rendering.

	/** Color of the debug ray drawn from the airplane's current position to each visible object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	FColor RayDebugColor = FColor::Yellow;

	/**
	 * Draws a wireframe of the camera's current view frustum every scan: four edges from the
	 * camera out to the far corners at ScanRadiusMeters, plus the far rectangle connecting them.
	 * Same shape/convention as UCesiumSurroundingsScannerComponent's bDrawScanArea.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	bool bDrawScanArea = true;

	/** Color of the scan-area wireframe (see bDrawScanArea). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	FColor ScanAreaDebugColor = FColor::Cyan;

private:
	/**
	 * Parses ObjectsJson into AllObjects, converting each entry's Latitude/Longitude/
	 * AltitudeMeters to WorldLocationMeters via Georeference. Called once from BeginPlay.
	 */
	void LoadObjects();

	/**
	 * Draws the bDrawScanArea wireframe (see its comment): four edges from Origin to the far
	 * corners at Range, plus the far rectangle connecting them. Identical shape logic to
	 * UCesiumSurroundingsScannerComponent::DrawScanAreaDebug, minus the lower-half cutoff (there
	 * is no ray-grid sweep here to cut down).
	 */
	void DrawScanAreaDebug(const FTransform& OriginTransform, float Range, float HalfHFovRad, float HalfVFovRad) const;

	/** ObjectStorage operation 1/2: registers a newly-visible object and logs its discovery. */
	void AddObject(const FString& Key, const FCustomSurroundingObject& Entry);

	/** ObjectStorage operation 2/2: forgets an object once it has left the camera's frame or range. */
	void RemoveObject(const FString& Key);

	/**
	 * Builds and caches this tick's IUAVSensorInterface JSON payload from ObjectStorage — one
	 * object per currently-visible entry, with id/type/latitude/longitude/altitude plus
	 * pixel_x/pixel_y/visible from ProjectWorldToScreen. Called at the end of TickComponent,
	 * after Scan(); does nothing (and drops the cached frame) while bSensorEnabled is false.
	 */
	void BuildSensorFrame();

	/**
	 * Lazily reads SensorSizeX/SensorSizeY from SceneCaptureComponent's TextureTarget. Split out
	 * from BuildSensorFrame() (which only runs while bSensorEnabled is true) so
	 * ProjectWorldToScreen has valid sensor dimensions for Scan()'s visibility test regardless of
	 * whether ZMQ sensor publishing is enabled. Called unconditionally at the top of
	 * TickComponent, before Scan().
	 */
	void UpdateSensorSize();

	/**
	 * Projects a single world-space point (Unreal cm) onto SceneCaptureComponent's render
	 * target. Identical view/projection matrix setup to
	 * UCesiumSurroundingsScannerComponent::ProjectWorldToScreen. Returns false (not visible) if
	 * the capture's render target size isn't known yet, the point is behind the camera, or it
	 * falls outside the render target bounds.
	 */
	bool ProjectWorldToScreen(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const;

	/** Owner's scene capture — supplies the transform the visibility test is projected from. */
	UPROPERTY()
	USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	/**
	 * Resolved in BeginPlay via ACesiumGeoreference::GetDefaultGeoreference. Used once by
	 * LoadObjects() to convert each entry's lat/long/altitude to a world-space position.
	 */
	UPROPERTY()
	ACesiumGeoreference* Georeference = nullptr;

	/**
	 * Every object parsed from ObjectsJson, with WorldLocationMeters precomputed. Populated once
	 * in BeginPlay; only DistanceMeters is refreshed afterwards (per Scan()).
	 */
	TArray<FCustomSurroundingObject> AllObjects;

	/**
	 * Persistent storage of currently-visible objects, keyed by ObjectID. Only ever mutated via
	 * AddObject()/RemoveObject() — see class docs. LatestScanResults, the console log, the debug
	 * rays, and the sensor payload are all driven from this, not from AllObjects directly.
	 */
	TMap<FString, FCustomSurroundingObject> ObjectStorage;

	/** SceneCaptureComponent's render-target resolution — lazily read in UpdateSensorSize(). */
	int32 SensorSizeX = 0;
	int32 SensorSizeY = 0;

	// Latest serialized IUAVSensorInterface frame — written and read on the game thread only.
	TArray<uint8> LatestPayload;
	double        LatestTimestamp = 0.0;
	bool          bHasFrame       = false;
};
