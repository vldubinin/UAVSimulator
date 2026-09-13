#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "KeyPointComponent.generated.h"

/**
 * Позначає ключову точку в світовому просторі на акторі-власнику.
 * Додайте по одному екземпляру на кожну точку в Blueprint, розташуйте відносно mesh
 * і задайте PointID як унікальний рядковий ідентифікатор.
 * UKeyPointDetectionComponent автоматично збирає всі екземпляри в BeginPlay.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UKeyPointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UKeyPointComponent();

	/** Унікальний рядковий ідентифікатор цієї ключової точки (наприклад, "nose", "left_wing_tip"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint")
	FString PointID = TEXT("keypoint");
};
