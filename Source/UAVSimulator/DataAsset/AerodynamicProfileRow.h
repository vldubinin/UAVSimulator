// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Curves/CurveFloat.h"
#include "AerodynamicProfileRow.generated.h"


USTRUCT(BlueprintType)
struct FAerodynamicProfileRow : public FTableRowBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Кут повороту закрилки"))
        float FlapAngle = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Крива підйомної сили до кута атаки"))
        FRuntimeFloatCurve ClVsAoA;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Крива супротиву повітря до кута атаки"))
        FRuntimeFloatCurve CdVsAoA;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Крива коефіцієнт моменту тангажу до кута атаки"))
        FRuntimeFloatCurve CmVsAoA;
};
