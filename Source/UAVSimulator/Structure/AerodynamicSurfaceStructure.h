#pragma once

#include "CoreMinimal.h" 
#include "Engine/DataTable.h"
#include "Runtime/Core/Public/Math/Vector.h"
#include "AerodynamicSurfaceStructure.generated.h" 

USTRUCT(BlueprintType)
struct FAerodynamicSurfaceStructure : public FTableRowBase
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		float ChordSize = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		FVector Offset = FVector(0.f, 0.f, 0.f);
};
