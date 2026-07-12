#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"
#include "SensorUtilityLibrary.generated.h"

UCLASS()
class UAVSIMULATOR_API USensorUtilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	static TArray<FHitResult> FindActors(const UObject* WorldContextObject,
		const FTransform& OriginTransform,
		const AActor* ActorToIgnore,
		float Range,
		int32 HorizontalRays,
		int32 VerticalLayers,
		float VerticalFOVDeg,
		ECollisionChannel CollisionChannel,
		bool bTraceComplex = false);
};