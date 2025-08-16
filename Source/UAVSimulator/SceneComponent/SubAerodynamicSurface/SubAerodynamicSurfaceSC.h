// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextRenderComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Entity/Chord.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "Runtime/Core/Public/Math/Vector.h"
#include "SubAerodynamicSurfaceSC.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UAVSIMULATOR_API USubAerodynamicSurfaceSC : public USceneComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	USubAerodynamicSurfaceSC();

private:
	TArray<FVector> Start3DProfile;
	TArray<FVector> End3DProfile;
	Chord StartChord;
	Chord EndChord;

private:
	FVector CenterOfPressure;
	float SurfaceArea;
	float DistanceToCenterOfMass;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

private:
	FVector FindCenterOfPressure(float PercentageOffset);
	float CalculateQuadSurfaceArea();

	void DrawSurface(FName SplineName);
	void DrawSpline(TArray<FVector> Points, FName SplineName);
	void DrawSplineCrosshairs(FVector Point, FName SplineName);
	void DrawText(FString Text, FVector Point, FVector Offset, FRotator Rotator, FColor Color, FName SplineName);

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void InitComponent(TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile, FName SurfaceName, float CenterOfPressureOffset, FVector GlobalSurfaceCenterOfMass);
	void CalculateEffectOfForcesOnSurface(FVector AirflowDirection);
};
