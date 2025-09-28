// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileRow.h"
#include "UAVSimulator/Structure/AerodynamicSurfaceStructure.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "UAVSimulator/Entity/SubSurface.h"
#include "UAVSimulator/SceneComponent/SubAerodynamicSurface/SubAerodynamicSurfaceSC.h"
#include "Runtime/Engine/Public/DrawDebugHelpers.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
#include "AirfoilData.h"

#include "AerodynamicSurfaceSC.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UAVSIMULATOR_API UAerodynamicSurfaceSC : public USceneComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UAerodynamicSurfaceSC();

public:	
	void OnConstruction(FVector CenterOfMass);
	AerodynamicForce CalculateForcesOnSurface(FVector CenterOfMass, FVector LinearVelocity, FVector AngularVelocity, FVector AirflowDirection);

private:
	TArray<FAirfoilPointData> GetPoints();
	void BuildSubsurfaces(FVector CenterOfMass, int32 Direction);
	void DestroySubsurfaces();
	
private:
	TArray<USubAerodynamicSurfaceSC*> SubSurfaces;

protected:
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Аеродинамічний профіль"))
		UDataTable* Profile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Форма поверхні"))
		TArray<FAerodynamicSurfaceStructure> SurfaceForm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Зміщення аеродинамічного центру (%)"))
		float AerodynamicCenterOffsetPercent = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Симетрична поверхня"))
		bool Mirror = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = ""))
		bool Enable = true;
};
