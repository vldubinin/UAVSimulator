#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Math/RandomStream.h"
#include "UAVSimulator/Structure/CustomSurroundingObject.h"

#include "YoloMarkerDatasetActor.generated.h"

class ACesiumGeoreference;
class ACesium3DTileset;
class ACesiumCameraManager;
class FJsonValue;

/**
 * Editor/runtime tool actor — the map-marker analogue of ADroneDatasetGeneratorActor
 * (the "Spherical Contour" tool). Instead of orbiting a spawned drone Blueprint and
 * extracting a silhouette, it orbits a USceneCaptureComponent2D around every marker
 * from UCustomSurroundingsScannerComponent's JSON source and writes a YOLO detection
 * dataset: one RGB frame per camera pose plus the pixel bounding box of EVERY marker
 * visible in that frame.
 *
 * Marker source: the same JSON schema as UCustomSurroundingsScannerComponent — an
 * array of {elementId, type, altitude, bbox:{x_min,x_max,y_min,y_max}} where every
 * corner is a {latitude, longitude} pair (see Tools/TestingPlatform/attitude_control/
 * marker/map_objects.json; "altitude" is optional). Each corner is converted to world
 * space via ACesiumGeoreference
 * and snapped straight down/up onto the Cesium tile surface (ResolveGroundHeights),
 * exactly as the scanner does — the JSON "altitude" is not used for placement.
 *
 * Angular coverage: every marker takes its turn as the orbit centre, so it is
 * guaranteed a full sweep — azimuth 0..360 by AzimuthStep, elevation
 * ElevationMinDeg..ElevationMaxDeg by ElevationStep, once per entry in
 * OrbitRadiiMeters. Markers that happen to fall inside another marker's frame are
 * labelled there too, for free.
 *
 * Runtime model: NOT a blocking loop. GenerateDataset() enables Tick and drives a
 * small state machine — move camera, wait SettleFrames ticks (so Cesium streams the
 * tiles under the new pose), CaptureScene() + ReadPixels, project + save, advance.
 * Because it needs a live streaming world it must be run during Play (from the
 * Synthetic Data menu section), like the other sensor-driven tools.
 */
UCLASS(Blueprintable)
class UAVSIMULATOR_API AYoloMarkerDatasetActor : public AActor
{
	GENERATED_BODY()

public:
	AYoloMarkerDatasetActor();

	virtual void Tick(float DeltaSeconds) override;

	// ── Marker source ─────────────────────────────────────────────────────────
	/** Absolute path to a JSON file with the UCustomSurroundingsScannerComponent schema
	 *  (bare array, or a { "objects": [...] } / { "markers": [...] } wrapper).
	 *  If set and readable it wins over ObjectsJsonInline. Defaults to the project's
	 *  Tools/TestingPlatform/attitude_control/marker/map_objects.json; if that path is
	 *  missing, LoadObjects() also tries the legacy .../attitude_control/map_objects.json. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Source")
	FString ObjectsSourceFilePath;

	/** Inline fallback used when ObjectsSourceFilePath is empty or unreadable. Same
	 *  schema as UCustomSurroundingsScannerComponent::ObjectsJson. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Source", meta = (MultiLine = true))
	FString ObjectsJsonInline;

	/** When non-empty, only the marker with this "elementId" is swept (still labelled
	 *  against every other marker that lands in frame). Handy for quick iteration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Source")
	FString OnlyMarkerId;

	/** 0 = all. Otherwise keep only the first N parsed markers — quick smoke tests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Source", meta = (ClampMin = 0))
	int32 MaxMarkers = 0;

	// ── Camera ────────────────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 32))
	int32 RenderWidth = 1280;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 32))
	int32 RenderHeight = 720;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 10.0, ClampMax = 150.0))
	float CameraFOV = 70.0f;

	// ── Sweep ─────────────────────────────────────────────────────────────────
	/** Camera-to-marker distances, in metres. One full azimuth/elevation sphere is
	 *  swept per entry, so more radii → more scale variation in the dataset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep")
	TArray<float> OrbitRadiiMeters;

	/** Azimuth step in degrees around the marker's local "up" axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 1.0, ClampMax = 180.0))
	float AzimuthStep = 45.0f;

	/** Lowest elevation ring, in degrees above the local horizon (0 = level with the marker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0, ClampMax = 89.0))
	float ElevationMinDeg = 20.0f;

	/** Highest elevation ring, in degrees (90 = straight down onto the marker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 1.0, ClampMax = 90.0))
	float ElevationMaxDeg = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 1.0, ClampMax = 90.0))
	float ElevationStep = 25.0f;

	/** Random yaw/pitch wobble (± degrees) added to the look direction so the target
	 *  is not always dead-centre — better for detector training. 0 disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0, ClampMax = 30.0))
	float AimJitterDeg = 4.0f;

	/** Extrude each footprint quad this far up the local vertical before projecting, so
	 *  the box covers a building's height, not just its ground outline. 0 = footprint only
	 *  (matches UCustomSurroundingsScannerComponent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0))
	float MarkerHeightMeters = 0.0f;

	/** Raise the whole orbit dome this far above the marker along local up. The camera
	 *  still aims at the marker itself; this only lifts the trajectory so low-elevation
	 *  rings form a raised hemisphere instead of skimming the tile surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0))
	float DomeLiftMeters = 5.0f;

	/** Minimum vertical gap (metres) between the camera and the Cesium tile surface
	 *  directly below/above it. Every pose is pushed straight up along local up until it
	 *  clears this, so the camera never dips under or lies on the terrain. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0))
	float CameraGroundClearanceMeters = 15.0f;

	// ── Label acceptance ──────────────────────────────────────────────────────
	/** Drop a marker's box if its on-screen width or height is below this (pixels). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter", meta = (ClampMin = 0.0))
	float MinBBoxPixels = 12.0f;

	/** A marker is labelled only if at least this fraction of its projected box lies
	 *  inside the frame (0.5 = more than half must be in view; 1 = fully on-screen).
	 *  Markers that pass are recorded with the box CLIPPED to the frame, so a partially
	 *  visible marker gets a box covering only its visible part. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MinVisibleFraction = 0.5f;

	/** Discard the whole frame unless the orbit's target marker itself produced a
	 *  valid box (skips poses where terrain/other buildings hide the target). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter")
	bool bRequireTargetVisible = true;

	/** Only label a marker if the camera has a clear line of sight to it. Without this
	 *  every marker whose footprint falls inside the frustum is boxed, including ones
	 *  hidden behind terrain or buildings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter")
	bool bRequireLineOfSight = true;

	/** Slack (metres) when comparing the line-of-sight hit distance to the marker
	 *  distance — covers ground-snapped points sitting flush on a tile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter", meta = (ClampMin = 0.0))
	float LineOfSightToleranceMeters = 3.0f;

	/** Drop a marker's box if it is farther than this from the camera (metres).
	 *  0 disables the distance cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter", meta = (ClampMin = 0.0))
	float MaxLabelDistanceMeters = 0.0f;

	// ── Ground snapping (mirrors UCustomSurroundingsScannerComponent) ──────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Ground")
	bool bSnapMarkersToTileSurface = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Ground")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Ground", meta = (ClampMin = 1.0))
	float GroundTraceSpanMeters = 20000.0f;

	// ── Timing ────────────────────────────────────────────────────────────────
	/** Minimum ticks to hold each pose before capturing (lets the camera move and the
	 *  Cesium view registration propagate). The real wait is gated on tile load
	 *  progress below — this is just the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Timing", meta = (ClampMin = 1))
	int32 SettleFrames = 8;

	/** Hard cap on ticks spent waiting for one pose. If the tileset never reports
	 *  ready within this many ticks the frame is captured anyway (and logged). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Timing", meta = (ClampMin = 1))
	int32 MaxSettleFrames = 180;

	/** Capture once ACesium3DTileset::GetLoadProgress() for the shot's view reaches
	 *  this percentage. Lower it (e.g. 98) if 100 is rarely hit on your connection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Timing", meta = (ClampMin = 1.0, ClampMax = 100.0))
	float TileLoadProgressTarget = 100.0f;

	/** Require the "tiles ready" state for this many consecutive ticks before
	 *  capturing — absorbs the brief drop from 100 as Cesium discovers new tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Timing", meta = (ClampMin = 1))
	int32 TileReadyHoldFrames = 3;

	// ── Output ────────────────────────────────────────────────────────────────
	/** Dataset root. Gets images/{train,val}/, labels/{train,val}/, data.yaml, dataset.json. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	FString OutputRootDir;

	/** Fraction of saved frames routed to images/val (0 = everything to train). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output", meta = (ClampMin = 0.0, ClampMax = 0.9))
	float ValSplit = 0.15f;

	/** Also write annotated PNGs (boxes + labels drawn) into <root>/debug/. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	bool bSaveDebugImages = false;

	/** Save frames as JPEG (matches the custom_objects_dataset pipeline); PNG when false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	bool bSaveAsJpeg = true;

	/** Start the sweep. Requires a running (Play) world — see the class comment. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void GenerateDataset();

	/** Stop early; whatever has been captured is flushed to data.yaml / dataset.json. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void CancelGeneration();

private:
	/** One camera pose to render: which marker is the orbit centre, and where the camera sits. */
	struct FShot
	{
		int32 MarkerIdx;
		float RadiusMeters;
		float AzimuthDeg;
		float ElevationDeg;
	};

	/** One accepted marker box in a frame — pixel-space top-left + size, plus the
	 *  marker centre's spatial coordinates so a detection can be tied back to a place. */
	struct FLabel
	{
		int32   ClassId;
		FString Id;
		FString Type;
		float   X;
		float   Y;
		float   W;
		float   H;
		float   VisibleFraction;

		FVector CentreWorldM;   // marker centre, Unreal world space, metres
		FVector CentreGeo;      // (longitude°, latitude°, height above ellipsoid in m)
		FVector CentreCameraM;  // marker centre in camera-local axes (X fwd, Y right, Z up), metres
		double  RangeM;         // straight-line camera → marker centre distance, metres
	};

	UPROPERTY() USceneCaptureComponent2D* CaptureComp   = nullptr;
	UPROPERTY() UTextureRenderTarget2D*   RenderTarget  = nullptr;
	UPROPERTY() ACesiumGeoreference*      Georeference  = nullptr;
	UPROPERTY() ACesium3DTileset*         Tileset       = nullptr;

	/** Cesium camera manager for this world; the sweep feeds it the capture pose so
	 *  Cesium actively streams/refines tiles for each shot instead of relying on
	 *  SettleFrames alone. Resolved lazily, self-nulls. */
	TWeakObjectPtr<ACesiumCameraManager> CesiumCameraManager;

	/** Stable id from ACesiumCameraManager::AddCamera; INDEX_NONE while unregistered. */
	int32 CesiumCameraId = INDEX_NONE;

	// ── Run state ─────────────────────────────────────────────────────────────
	bool  bRunning           = false;
	int32 ShotCursor         = 0;
	int32 SettleCounter      = 0;
	int32 ReadyStreak        = 0;   // consecutive ticks the tileset has reported ready
	int32 SavedFrameCount    = 0;
	int32 AttemptedShotCount = 0;
	int32 ValEveryN          = 6;
	FRandomStream Rng;

	TArray<FCustomSurroundingObject> Objects;
	TArray<FShot>                    Shots;
	TMap<FString, int32>             ClassMap;    // ObjectType → class id
	TArray<FString>                  ClassNames;  // class id → name
	TArray<TSharedPtr<FJsonValue>>   ManifestFrames;

	/** Every ACesium3DTileset in the level, plus the culling flags they had before the
	 *  sweep forced ForbidHoles / disabled fog culling. Restored in FinishGeneration. */
	struct FTilesetCulling { bool ForbidHoles; bool FogCulling; };
	UPROPERTY() TArray<TObjectPtr<ACesium3DTileset>> SweepTilesets;
	TArray<FTilesetCulling>                          SavedTilesetCulling;

	FString ImagesTrainDir;
	FString ImagesValDir;
	FString LabelsTrainDir;
	FString LabelsValDir;
	FString DebugDir;

	/** "<frame_stem> <lat> <lon> <alt_m>" per saved frame → <root>/exp_geo_position.txt. */
	TArray<FString> GeoPositionLines;

	// ── Pipeline steps ────────────────────────────────────────────────────────
	bool LoadObjects();
	void BuildClassMap();
	void BuildShots();

	FVector GeoToWorldMeters(double LatitudeDeg, double LongitudeDeg, double HeightMeters) const;
	/** Inverse of GeoToWorldMeters: Unreal world metres → (longitude°, latitude°, height m). */
	FVector WorldMetersToGeographic(const FVector& WorldMeters) const;
	FVector GeographicUpMeters(double LatitudeDeg, double LongitudeDeg) const;
	void    ResolveGroundHeights(FCustomSurroundingObject& Object) const;

	/** Force ForbidHoles on / fog culling off for every tileset for the duration of the
	 *  sweep (so frames never contain black tile gaps), and restore afterwards. */
	void  BeginTilesetCaptureMode();
	void  EndTilesetCaptureMode();
	/** Lowest GetLoadProgress() across all sweep tilesets — the shot is only captured
	 *  once this reaches TileLoadProgressTarget. 100 when there are no tilesets. */
	float MinTilesetLoadProgress() const;

	void PlaceCameraForShot(const FCustomSurroundingObject& Target, const FShot& Shot);
	/** Push a camera position straight up along UpDir until it clears the Cesium tile
	 *  surface by CameraGroundClearanceMeters. Returns the (possibly raised) position. */
	FVector LiftAboveTileSurface(const FVector& CamPosCm, const FVector& UpDir) const;
	void SyncCesiumCaptureCamera();        // add-or-update the FCesiumCamera from the current pose
	void UnregisterCesiumCaptureCamera();  // remove it when the sweep ends
	bool ProjectMetersToPixels(const FVector& WorldMeters, FVector2D& OutPixels) const;
	bool ComputeMarkerLabel(const FCustomSurroundingObject& Object, FLabel& OutLabel) const;
	/** True if the camera has an unobstructed line of sight to any test point of the marker. */
	bool IsMarkerVisibleFromCamera(const FCustomSurroundingObject& Object) const;

	void ProcessCurrentShot();
	bool SaveFrame(const TArray<FColor>& Pixels, const TArray<FLabel>& Labels,
	               const FShot& Shot, const FString& TargetId);
	void WriteDatasetYaml() const;
	void WriteManifest() const;
	/** classes.json + virtual_map.json (id → lat/lon of every marker) + exp_geo_position.txt. */
	void WriteReferenceFiles() const;
	void FinishGeneration(bool bCancelled);
};
