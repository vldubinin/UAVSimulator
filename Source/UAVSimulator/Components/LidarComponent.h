#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "LidarComponent.generated.h"

/**
 * LiDAR sensor mounted at a configurable position on the aircraft.
 * Each scan fires rays in a spherical pattern and populates LatestScanResults:
 *   actor name → closest hit distance (in Unreal cm).
 *
 * Implements IUAVSensorInterface — SensorBusComponent calls GetLatestFrame()
 * each bus tick to retrieve the scan results as a JSON-encoded payload.
 *
 * As a USceneComponent the sensor can be placed anywhere in the actor's
 * component hierarchy and its transform is used as the scan origin.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API ULidarComponent : public USceneComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	ULidarComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("lidar"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/**
	 * Runs a full scan immediately on the game thread, updates LatestScanResults,
	 * and returns a reference to it. Called automatically by TickComponent at
	 * ScanRate Hz; can also be triggered directly from Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar")
	const TMap<FString, float>& Scan();

	/** Most recent scan results: actor name → closest hit distance in Unreal cm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lidar")
	TMap<FString, float> LatestScanResults;

	// ── Scan parameters ───────────────────────────────────────────────────────

	/** Maximum detection range in Unreal cm (default 5000 cm = 50 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 1.0f))
	float Range = 5000.0f;

	/** Number of rays distributed evenly over a full 360° horizontal sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 1))
	int32 HorizontalRays = 360;

	/** Number of evenly-spaced vertical scan layers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 1))
	int32 VerticalLayers = 16;

	/** Total vertical field of view in degrees, symmetric around the horizontal plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 1.0f, ClampMax = 180.0f))
	float VerticalFOVDeg = 30.0f;

	/** How many full scans are performed per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 0.1f, ClampMax = 100.0f))
	float ScanRate = 10.0f;

	/** Collision channel used for ray traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

private:
	float  GetVerticalAngle(int32 V) const;

	float  ScanAccumulator      = 0.0f;
	double LatestScanTimestamp  = 0.0;   // world time when Scan() last completed
};
