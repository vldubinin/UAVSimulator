#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"

#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>
#include "PostOpenCVHeaders.h"

#include "DroneDatasetGeneratorActor.generated.h"

/**
 * Editor tool actor. Place in any level, assign DroneBlueprintClass,
 * configure the spherical sweep parameters and OutputJsonPath, then click the
 * "Generate Dataset" button in the Details panel.
 *
 * Mask strategy: uses PRM_UseShowOnlyList so only the spawned drone is rendered
 * against a guaranteed-black background. No post-process material dependency.
 * If MaskPostProcessMaterial is assigned it is injected on top (optional).
 *
 * For each (azimuth, elevation) pair the actor:
 *   1. Spawns the drone, positions the capture camera on the orbit sphere.
 *   2. Temporarily switches the capture to show-only-drone mode, calls
 *      CaptureScene(), flushes via ReadPixels().
 *   3. Extracts a contour with OpenCV (Otsu threshold + morph-close + approxPolyDP).
 *   4. Accumulates all frames into a JSON file and optionally saves debug PNGs.
 */
UCLASS(Blueprintable)
class UAVSIMULATOR_API ADroneDatasetGeneratorActor : public AActor
{
	GENERATED_BODY()

public:
	ADroneDatasetGeneratorActor();

	// ── Setup ──────────────────────────────────────────────────────────────────
	/** Drone Blueprint to render. Drag it here from the Content Browser. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Setup")
	TSubclassOf<AActor> DroneBlueprintClass;

	/** Optional. Post-process material injected during capture (e.g. custom stencil
	 *  toon-shader). Leave null — the show-only mask approach works without it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Setup")
	UMaterialInterface* MaskPostProcessMaterial = nullptr;

	// ── Camera ─────────────────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera",
		meta = (ClampMin = 10.0, ClampMax = 150.0))
	float CameraFOV = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 32))
	int32 RenderWidth = 640;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 32))
	int32 RenderHeight = 480;

	// ── Sweep ──────────────────────────────────────────────────────────────────
	/** Camera orbit radius around the drone's actor origin, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 50.0))
	float OrbitRadius = 2000.0f;

	/** Azimuth sweep step in degrees. 0° starts along the +X axis, increases CCW. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep",
		meta = (ClampMin = 1.0, ClampMax = 180.0))
	float AzimuthStep = 45.0f;

	/** Elevation ring spacing in degrees. Rings are placed at −90+step, −90+2·step, …, 90−step.
	 *  The north (El=90) and south (El=−90) poles are always included as single frames. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep",
		meta = (ClampMin = 1.0, ClampMax = 89.0))
	float ElevationStep = 30.0f;

	// ── OpenCV ─────────────────────────────────────────────────────────────────

	/** Kernel size for Gaussian blur applied before thresholding (pixels, must be odd).
	 *  Smooths jagged silhouette edges; larger values give a softer boundary.
	 *  Set to 1 to disable blurring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Processing",
		meta = (ClampMin = 1, ClampMax = 31))
	int32 SilhouetteBlurSize = 7;

	/** Morphological close kernel size (pixels).
	 *  Merges thin protrusions — landing gear legs, antennas, wing struts — into
	 *  the main silhouette body. Increase if small parts are still detaching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Processing",
		meta = (ClampMin = 1, ClampMax = 99))
	int32 FillGapsSize = 15;

	/** Morphological open kernel size (pixels).
	 *  Erases isolated blobs that survive the fill-gaps pass.
	 *  Keep smaller than FillGapsSize to avoid eroding the drone itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Processing",
		meta = (ClampMin = 1, ClampMax = 99))
	int32 RemoveIslandsSize = 9;

	/** Polygon approximation tolerance: fraction of the contour perimeter.
	 *  Larger value → fewer vertices, smoother shape. 0.02 is a good starting point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Processing",
		meta = (ClampMin = 0.001, ClampMax = 0.5))
	float PolygonSmoothness = 0.02f;

	// ── Output ─────────────────────────────────────────────────────────────────
	/** Absolute path for the output JSON file, e.g. C:/Datasets/drone.json */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	FString OutputJsonPath;

	/** When true, saves one PNG per frame with the polygon overlaid on the mask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	bool bSaveDebugImages = true;

	/** Directory for debug images. Leave empty to auto-create a subfolder next to
	 *  OutputJsonPath named <json-basename>_frames/. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output",
		meta = (EditCondition = "bSaveDebugImages"))
	FString OutputImageDir;

	/** Click this button to start dataset generation. Blocks the editor until done. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void GenerateDataset();

private:
	struct FFrameData
	{
		float             Azimuth;
		float             Elevation;
		TArray<FVector2D> Polygon;
	};

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComp = nullptr;

	void              PlaceCameraAt(const FVector& Target, float AzimuthDeg, float ElevationDeg);
	bool              CaptureMask(AActor* DroneActor, UTextureRenderTarget2D* RT, TArray<FColor>& OutPixels);
	TArray<FVector2D> ExtractPolygon(const TArray<FColor>& Pixels) const;
	void              SaveDebugImage(const TArray<FColor>& Pixels, const TArray<FVector2D>& Polygon,
	                                 float AzimuthDeg, float ElevationDeg,
	                                 const FString& ImageDir, int FrameId) const;
	FString           BuildJson(const FString& ModelName, const TArray<FFrameData>& Frames) const;
};
