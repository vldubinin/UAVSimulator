#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "KeyPointComponent.generated.h"

/**
 * Marks a world-space keypoint on the owner actor.
 * Add one per keypoint in Blueprint, position it relative to the mesh,
 * and set PointID to a unique string identifier.
 * UKeyPointDetectionComponent collects all instances automatically in BeginPlay.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UKeyPointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UKeyPointComponent();

	/** Unique string identifier for this keypoint (e.g. "nose", "left_wing_tip"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint")
	FString PointID = TEXT("keypoint");
};
