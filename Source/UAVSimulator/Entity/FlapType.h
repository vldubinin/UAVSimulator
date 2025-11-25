// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FlapType.generated.h"


UENUM(BlueprintType)
enum class EFlapType : uint8
{
    None UMETA(DisplayName = "Немає"),
    Aileron UMETA(DisplayName = "Елерон"),
    Elevator UMETA(DisplayName = "Руль висоти"),
    Rudder UMETA(DisplayName = "Руль повороту")
};