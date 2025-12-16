// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AxisType.generated.h"


UENUM(BlueprintType)
enum class EAxisType : uint8
{
    X UMETA(DisplayName = "X"),
    Y UMETA(DisplayName = "Y"),
    Z UMETA(DisplayName = "Z")
};