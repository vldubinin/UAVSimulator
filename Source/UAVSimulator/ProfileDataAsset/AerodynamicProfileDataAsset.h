// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h" 
#include "Engine/DataTable.h"
#include "Curves/CurveFloat.h"
#include "Components/SplineComponent.h"
#include "AerodynamicProfileDataAsset.generated.h"

UCLASS(BlueprintType, Blueprintable)
class UAVSIMULATOR_API UAerodynamicProfileDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Аеродинамічний профіль"))
		UDataTable* Profile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Крива підйомної сили до кута атаки"))
		UCurveFloat* ClVsAoA;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Крива супротиву повітря до кута атаки"))
		UCurveFloat* CdVsAoA;

};