#pragma once

#include "CoreMinimal.h" 
#include "Engine/DataTable.h"
#include "AerodynamicProfileStructure.generated.h" 

USTRUCT(BlueprintType)
struct FAerodynamicProfileStructure : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
		float X = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
		float Z = 0.0f;
};
