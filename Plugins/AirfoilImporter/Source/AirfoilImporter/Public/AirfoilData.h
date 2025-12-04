// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AirfoilImporter.h"
#include "Engine/DataTable.h"
#include "AirfoilData.generated.h"


USTRUCT(BlueprintType)
struct AIRFOILIMPORTER_API FAirfoilPointData : public FTableRowBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airfoil Data")
	float X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airfoil Data")
	float Z;

	FAirfoilPointData() : X(0.f), Z(0.f) {}
};
