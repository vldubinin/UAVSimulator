#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/SceneComponent/KeyPoint/KeyPointComponent.h"

#include "DroneKeyPointDatasetActor.generated.h"

/**
 * Editor tool actor. Place in any level, assign DroneBlueprintClass (must have
 * UKeyPointComponent instances placed on it in Blueprint), set OutputJsonPath,
 * then click "Export Key Points".
 *
 * Spawns the drone, reads every UKeyPointComponent's position in the drone's
 * local coordinate system, and exports dimensionless (normalised) 3D coordinates.
 *
 * Normalisation: all local positions are divided by the maximum absolute coordinate
 * value found across all keypoints, so every component falls in [-1, 1].
 * The raw scale factor (in cm) is stored in the JSON so positions can be recovered.
 *
 * JSON output:
 * {
 *   "drone_model": "MyDrone_C",
 *   "scale_cm": 245.3,
 *   "keypoints": [
 *     { "id": "nose",       "x":  0.501, "y":  0.000, "z":  0.120 },
 *     { "id": "left_wing",  "x": -0.980, "y": -0.100, "z":  0.050 }
 *   ]
 * }
 */
UCLASS(Blueprintable)
class UAVSIMULATOR_API ADroneKeyPointDatasetActor : public AActor
{
	GENERATED_BODY()

public:
	ADroneKeyPointDatasetActor();

	/** Drone Blueprint to sample. Must have UKeyPointComponent children placed on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	TSubclassOf<AActor> DroneBlueprintClass;

	/** Absolute path for the output JSON file, e.g. C:/Datasets/drone_keypoints.json */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	FString OutputJsonPath;

	/** Click to spawn the drone, collect keypoints and write the JSON file. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void ExportKeyPoints();

private:
	FString BuildJson(const FString& ModelName, float ScaleCm,
	                  const TArray<UKeyPointComponent*>& KPComps,
	                  const FTransform& DroneTransform) const;
};
