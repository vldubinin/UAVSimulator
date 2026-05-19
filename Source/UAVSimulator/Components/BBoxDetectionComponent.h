#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "Math/Box2D.h"

#include "BBoxDetectionComponent.generated.h"

/**
 * Detects scene actors via a lidar-style ray sweep from the owner's
 * SceneCaptureComponent2D, projects each actor's OBB to 2D screen space,
 * and publishes the resulting bounding boxes as JSON on the "bbox" topic.
 *
 * SceneActors are collected once on the first tick; their 2D projections
 * are recomputed every tick using the current camera transform.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UBBoxDetectionComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UBBoxDetectionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("bbox"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

public:
	/** Maximum ray-cast range in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox", meta = (ClampMin = 1.0f))
	float Range = 5000.0f;

	/** Number of rays distributed evenly over a full 360° horizontal sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox", meta = (ClampMin = 1))
	int32 HorizontalRays = 360;

	/** Number of evenly-spaced vertical scan layers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox", meta = (ClampMin = 1))
	int32 VerticalLayers = 16;

	/** How many full scans are performed per second (reserved for future rate-limiting). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox", meta = (ClampMin = 0.1f, ClampMax = 100.0f))
	float ScanRate = 10.0f;

	/** Collision channel used for ray traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

private:
	void CollectSceneActors();
	FBox2D ProjectActorToScreen(AActor* Actor) const;
	FString SerializeBBoxes(const TMap<FString, FBox2D>& BBoxMap) const;

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent = nullptr;

	float VerticalFOVDeg = 0.0f;

	TArray<AActor*> SceneActors;

	// Latest serialized frame — written and read on the game thread only.
	TArray<uint8> LatestPayload;
	double        LatestTimestamp = 0.0;
	bool          bHasFrame = false;
};
