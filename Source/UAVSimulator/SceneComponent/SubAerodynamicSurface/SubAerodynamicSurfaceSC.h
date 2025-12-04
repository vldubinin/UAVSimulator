// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextRenderComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Entity/Chord.h"
#include "Curves/CurveFloat.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileRow.h"
#include "Runtime/Core/Public/Math/Vector.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
#include "Engine/DataTable.h"
#include "UAVSimulator/Entity/ControlInputState.h"
#include "UAVSimulator/Entity/FlapType.h"
#include "UAVSimulator/SceneComponent/ControlSurface/ControlSurfaceSC.h"

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
	float StartFlopPosition;
	float EndFlopPosition;

	UDataTable* AerodynamicTable;

	const float AirDensity = 1.225f;
	const FVector Wind = FVector();
	bool IsMirror = false;
	EFlapType FlapType;
	UControlSurfaceSC* ControlSurface;

private:
	FVector CenterOfPressure;
	float SurfaceArea;
	float DistanceToCenterOfMass;

private:
	float CalculateFlapAngel(float StartFlopValue, float EndFlopValue, float InputSignal);
	float GetFlapAngel(ControlInputState ControlState);
	FVector FindCenterOfPressure(float PercentageOffset);
	float CalculateQuadSurfaceArea();
	float CalculateAngleOfAttackDeg(FVector WorldAirVelocity, FVector AverageChordDirection);
	float CalculateLiftInNewtons(float AoA, int FlapAngle, float DynamicPressure);
	float CalculateDragInNewtons(float AoA, int FlapAngle, float DynamicPressure);
	float CalculateTorqueInNewtons(float AoA, int FlapAngle, float DynamicPressure, float ChordLengt);
	float CalculateAvarageChordLength(Chord FirstChord, Chord SecondChord);
	float ToSpeedInMetersPerSecond(FVector WorldAirVelocity);
	FVector GetLiftDirection(FVector WorldAirVelocity);
	float NewtonsToKiloCentimeter(float Newtons);
	FVector GetPointOnLineAtPercentage(FVector StartPoint, FVector EndPoint, float Alpha);
	FAerodynamicProfileRow* GetProfile(int FlapAngle);

	void DrawSurface(FName SplineName);
	void DrawSpline(TArray<FVector> Points, FName SplineName);
	void DrawFlop(FName SplineName);
	void DrawSplineCrosshairs(FVector Point, FName SplineName);
	void DrawText(FString Text, FVector Point, FVector Offset, FRotator Rotator, FColor Color, FName SplineName);

public:	
	void InitComponent(TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile, 
		FName SurfaceName, float CenterOfPressureOffset, FVector GlobalSurfaceCenterOfMass, 
		float InStartFlopPosition, float InEndFlopPosition, UDataTable* ProfileAerodynamicTable, bool IsMirrorSurface, EFlapType FlapType, UControlSurfaceSC* ControlSurfaceSC);
	AerodynamicForce CalculateForcesOnSubSurface(FVector LinearVelocity, FVector AngularVelocity, FVector GlobalCenterOfMassInWorld, FVector AirflowDirection, ControlInputState ControlState);
};
