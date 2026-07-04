#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SceneObjectDatasetActor.generated.h"

/**
 * Editor/runtime tool actor. Place in any level, set OutputJsonPath, then click
 * "Export Scene Objects" (or trigger ExportSceneObjects() from the UI).
 *
 * Scans every actor currently in the world and exports its world-space position
 * and axis-aligned bounding box size (in Unreal units, same as position).
 * Player and target drones (AAirplane instances) are excluded by class check —
 * no tagging or other per-actor markup is required. Actors whose class name
 * matches an entry in ExcludedActorClassNames are also skipped. Actors with no
 * UStaticMeshComponent (game mode, controllers, volumes, subsystem actors, etc.)
 * are skipped since they are not physical scene objects.
 *
 * JSON output:
 * {
 *   "objects": [
 *     { "name": "Building_1", "class": "StaticMeshActor", "x": 120.0, "y": -50.0, "z": 0.0,
 *       "size_x": 200.0, "size_y": 150.0, "size_z": 300.0 }
 *   ]
 * }
 */
UCLASS(Blueprintable)
class UAVSIMULATOR_API ASceneObjectDatasetActor : public AActor
{
	GENERATED_BODY()

public:
	ASceneObjectDatasetActor();

	/** Absolute path for the output JSON file, e.g. C:/Datasets/scene_objects.json */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	FString OutputJsonPath = TEXT("C:/Datasets/scene_objects.json");

	/** Actor class names to exclude from the export (matched against GetClass()->GetName(),
	 *  e.g. "StaticMeshActor"). AAirplane instances are always excluded regardless of this list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	TArray<FString> ExcludedActorClassNames;

	/** Click to scan the scene and write the JSON file. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void ExportSceneObjects();

private:
	bool IsExcludedClass(const AActor* Actor) const;
	FString BuildJson(const TArray<AActor*>& Objects) const;
};
