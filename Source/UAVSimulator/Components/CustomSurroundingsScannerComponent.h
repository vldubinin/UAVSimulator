#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
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
 * ObjectsJson is parsed in BeginPlay (LoadObjects()): every entry's Latitude/Longitude/
 * AltitudeMeters is converted to a world-space position via
 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal — see AllObjects.
 * TickComponent re-parses it whenever the string changes (see LastLoadedObjectsJson), so editing
 * ObjectsJson — including a per-object "bboxw"/"bboxh" — takes effect live, without restarting.
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
 *
 * Georeference is used only to convert each object's lat/long/altitude into a world-space
 * position (LoadObjects()) — there is no further interaction with Cesium tile geometry.
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

	/**
	 * Draws a fixed-orientation rectangle (BBoxWidthMeters × BBoxHeightMeters, from ObjectsJson's
	 * "bboxw"/"bboxh") centered on each visible object's point — see DrawObjectBBoxDebug().
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	bool bDrawObjectBBox = true;

	/** Color of the object bbox rectangle (see bDrawObjectBBox). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	FColor BBoxDebugColor = FColor::Green;

	/** Draws each visible object's "elementId" (ObjectID) as text above its point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	bool bDrawObjectLabel = true;

	/** Color of the elementId label text (see bDrawObjectLabel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	FColor LabelDebugColor = FColor::White;

	/** Font scale of the elementId label text (see bDrawObjectLabel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug", meta = (ClampMin = 0.1f))
	float LabelFontScale = 1.5f;

private:
	/**
	 * Parses ObjectsJson into AllObjects, converting each entry's Latitude/Longitude/
	 * AltitudeMeters to WorldLocationMeters via Georeference. Called from BeginPlay, and again
	 * from TickComponent any time ObjectsJson no longer matches LastLoadedObjectsJson — so edits
	 * made while playing (including per-object "bboxw"/"bboxh") take effect on the next tick.
	 */
	void LoadObjects();

	/**
	 * Draws the bDrawScanArea wireframe (see its comment): four edges from Origin to the far
	 * corners at Range, plus the far rectangle connecting them. Identical shape logic to
	 * UCesiumSurroundingsScannerComponent::DrawScanAreaDebug, minus the lower-half cutoff (there
	 * is no ray-grid sweep here to cut down).
	 */
	void DrawScanAreaDebug(const FTransform& OriginTransform, float Range, float HalfHFovRad, float HalfVFovRad) const;

	/**
	 * Fills OutCorners with the four world-space corners (Unreal cm) of a WidthMeters ×
	 * HeightMeters rectangle centered on WorldPositionCm, flat against the fixed
	 * InitialCameraRightAxis/InitialCameraUpAxis orientation. Shared by DrawObjectBBoxDebug() and
	 * ComputeBBoxScreenSize() so both agree on exactly what shape a bbox is.
	 */
	void ComputeBBoxCorners(const FVector& WorldPositionCm, float WidthMeters, float HeightMeters, FVector (&OutCorners)[4]) const;

	/**
	 * Draws a rectangle of size WidthMeters × HeightMeters (converted to cm), centered on
	 * WorldPositionCm, flat against the fixed InitialCameraRightAxis/InitialCameraUpAxis
	 * orientation — not billboarded to the camera's current transform, so it doesn't rotate.
	 */
	void DrawObjectBBoxDebug(const FVector& WorldPositionCm, float WidthMeters, float HeightMeters) const;

	/**
	 * Projects WidthMeters × HeightMeters's four corners (ComputeBBoxCorners) onto the camera
	 * frame via ProjectWorldToScreenUnclamped, then returns the pixel-space width/height of their
	 * axis-aligned bounds — the same projection used for pixel_x/pixel_y, unclamped to the sensor
	 * so a partially off-frame bbox still reports its true size. A corner behind the camera is
	 * skipped; if all four are, returns (0, 0).
	 */
	FVector2D ComputeBBoxScreenSize(const FVector& WorldPositionCm, float WidthMeters, float HeightMeters) const;

	/** Draws Label as text at WorldPositionCm (see bDrawObjectLabel). */
	void DrawObjectLabelDebug(const FVector& WorldPositionCm, const FString& Label) const;

	/** ObjectStorage operation 1/2: registers a newly-visible object and logs its discovery. */
	void AddObject(const FString& Key, const FCustomSurroundingObject& Entry);

	/** ObjectStorage operation 2/2: forgets an object once it has left the camera's frame or range. */
	void RemoveObject(const FString& Key);

	/**
	 * Builds and caches this tick's IUAVSensorInterface JSON payload from ObjectStorage — one
	 * object per currently-visible entry, with id/type/latitude/longitude/altitude plus
	 * pixel_x/pixel_y/visible from ProjectWorldToScreen, and bboxw/bboxh as the pixel-space size of
	 * the projected bbox (ComputeBBoxScreenSize) — not the raw BBoxWidthMeters/BBoxHeightMeters.
	 * Called at the end of TickComponent, after Scan(); does nothing (and drops the cached frame)
	 * while bSensorEnabled is false.
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
	 * falls outside the render target bounds. Thin wrapper over ProjectWorldToScreenUnclamped that
	 * adds the bounds check.
	 */
	bool ProjectWorldToScreen(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const;

	/**
	 * Core of ProjectWorldToScreen, minus the sensor-bounds check: same view/projection matrix
	 * setup, but OutScreenPos is returned as-is even when it falls outside the render target (used
	 * by ComputeBBoxScreenSize, where an off-frame corner is still meaningful). Returns false only
	 * when the capture/sensor size isn't known yet or the point is behind the camera.
	 */
	bool ProjectWorldToScreenUnclamped(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const;

	/** Owner's scene capture — supplies the transform the visibility test is projected from. */
	UPROPERTY()
	USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	/**
	 * SceneCaptureComponent's Right/Up axes, captured once in BeginPlay (its first frame) and
	 * never updated again. DrawObjectBBoxDebug() draws every object's bbox flat against this fixed
	 * reference orientation instead of billboarding to the camera's current transform each tick,
	 * so a box doesn't rotate/twist as the airplane moves — it only translates with its object.
	 */
	FVector InitialCameraRightAxis = FVector::RightVector;
	FVector InitialCameraUpAxis    = FVector::UpVector;

	/**
	 * Resolved in BeginPlay via ACesiumGeoreference::GetDefaultGeoreference. Used once by
	 * LoadObjects() to convert each entry's lat/long/altitude to a world-space position.
	 */
	UPROPERTY()
	ACesiumGeoreference* Georeference = nullptr;

	/**
	 * Every object parsed from ObjectsJson, with WorldLocationMeters precomputed. Rebuilt by
	 * LoadObjects() (BeginPlay, and again whenever ObjectsJson changes); DistanceMeters is
	 * additionally refreshed every Scan().
	 */
	TArray<FCustomSurroundingObject> AllObjects;

	/** ObjectsJson as of the last LoadObjects() call — lets TickComponent detect live edits. */
	FString LastLoadedObjectsJson;

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
