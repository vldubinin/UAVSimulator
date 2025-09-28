// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AerodynamicProfileRow.h"
#include "AerodynamicProfileAndFlapRow.generated.h"

USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FAerodynamicProfileAndFlapRow : public FTableRowBase
{
	GENERATED_BODY()
	
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
        float FlapAngle = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
        FAerodynamicProfileRow AerodynamicProfileData;
};