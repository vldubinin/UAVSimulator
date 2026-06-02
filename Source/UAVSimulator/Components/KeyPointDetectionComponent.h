#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/SceneComponent/KeyPoint/KeyPointComponent.h"

#include "KeyPointDetectionComponent.generated.h"

/**
 * Placed on the OBSERVER actor (same actor as CameraFrameComponent and SensorBusComponent).
 * Uses the same lidar-style ray sweep as BBoxDetectionComponent to find nearby actors,
 * then projects each UKeyPointComponent found on those actors onto the observer's
 * SceneCaptureComponent2D, and publishes the results as JSON on the "keypoints" topic.
 *
 * UKeyPointComponent instances must be placed on the TARGET drone blueprint — NOT on
 * the observer. The observer discovers them automatically via the ray sweep.
 *
 * JSON output format (one payload per bus tick):
 * {
 *   "Cessna_172_C_0": [
 *     { "id": "nose",     "x": 320.5, "y": 240.1, "visible": true  },
 *     { "id": "left_wing","x":  10.2, "y": 198.3, "visible": false }
 *   ],
 *   "Cessna_172_C_1": [ ... ]
 * }
 * "visible" is false when the point is behind the camera or outside the image bounds.
 * x/y are always emitted so consumers can detect occlusion without changing array length.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UKeyPointDetectionComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UKeyPointDetectionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("keypoints"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

public:
	/** Maximum ray-cast range used to discover target actors (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint", meta = (ClampMin = 1.0f))
	float Range = 5000.0f;

	/** Number of horizontal rays in the discovery sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint", meta = (ClampMin = 1))
	int32 HorizontalRays = 360;

	/** Number of vertical layers in the discovery sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint", meta = (ClampMin = 1))
	int32 VerticalLayers = 16;

	/** Collision channel used for ray traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

private:
	struct FKeyPoint2D
	{
		FString   ID;
		FVector2D ScreenPosition;
		bool      bVisible;
	};

	/** Projects a single world-space point onto the render target.
	 *  Returns true if the point lands within the image bounds. */
	bool ProjectWorldToScreen(const FVector& WorldPos, FVector2D& OutScreenPos) const;

	/** Serializes all actors' keypoints into a single JSON object keyed by actor name. */
	FString SerializeAllKeyPoints(const TMap<FString, TArray<FKeyPoint2D>>& PerActorKeyPoints) const;

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent = nullptr;

	float VerticalFOVDeg = 0.0f;
	int32 SizeX          = 0;
	int32 SizeY          = 0;

	// Latest serialized frame — written and read on the game thread only.
	TArray<uint8> LatestPayload;
	double        LatestTimestamp = 0.0;
	bool          bHasFrame       = false;
};
