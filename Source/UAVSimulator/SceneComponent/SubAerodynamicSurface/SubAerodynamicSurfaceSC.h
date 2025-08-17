// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextRenderComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Entity/Chord.h"
#include "Curves/CurveFloat.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "UAVSimulator/ProfileDataAsset/AerodynamicProfileDataAsset.h"
#include "Runtime/Core/Public/Math/Vector.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
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

	UCurveFloat* ClVsAoA;
	UCurveFloat* CdVsAoA;
	UCurveFloat* CmVsAoA;

	const float AirDensity = 1.225f;
	const FVector Wind = FVector();

private:
	FVector CenterOfPressure;
	float SurfaceArea;
	float DistanceToCenterOfMass;

private:
	FVector FindCenterOfPressure(float PercentageOffset);
	float CalculateQuadSurfaceArea();
	float CalculateAngleOfAttackDeg(FVector WorldAirVelocity, FVector AverageChordDirection);
	float CalculateLiftInNewtons(float AoA, float DynamicPressure);
	float CalculateDragInNewtons(float AoA, float DynamicPressure);
	float CalculateTorqueInNewtons(float AoA, float DynamicPressure, float ChordLengt);
	float CalculateAvarageChordLength(Chord FirstChord, Chord SecondChord);
	float ToSpeedInMetersPerSecond(FVector WorldAirVelocity);
	FVector GetLiftDirection(FVector WorldAirVelocity);
	float NewtonsToKiloCentimeter(float Newtons);

	void DrawSurface(FName SplineName);
	void DrawSpline(TArray<FVector> Points, FName SplineName);
	void DrawSplineCrosshairs(FVector Point, FName SplineName);
	void DrawText(FString Text, FVector Point, FVector Offset, FRotator Rotator, FColor Color, FName SplineName);

public:	
	void InitComponent(TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile, FName SurfaceName, float CenterOfPressureOffset, FVector GlobalSurfaceCenterOfMass, UAerodynamicProfileDataAsset* AerodynamicProfile);
	AerodynamicForce CalculateForcesOnSubSurface(FVector LinearVelocity, FVector AngularVelocity, FVector GlobalCenterOfMassInWorld, FVector AirflowDirection);
};
