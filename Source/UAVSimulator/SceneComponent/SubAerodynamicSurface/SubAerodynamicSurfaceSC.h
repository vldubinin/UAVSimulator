// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Entity/Chord.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileRow.h"
#include "Engine/DataTable.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
#include "UAVSimulator/Entity/ControlInputState.h"
#include "UAVSimulator/Entity/FlapType.h"
#include "UAVSimulator/SceneComponent/ControlSurface/ControlSurfaceSC.h"

#include "SubAerodynamicSurfaceSC.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UAVSIMULATOR_API USubAerodynamicSurfaceSC : public USceneComponent
{
	GENERATED_BODY()

public:
	USubAerodynamicSurfaceSC();

	void InitComponent(TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile,
		FName SurfaceName, float CenterOfPressureOffset, FVector GlobalSurfaceCenterOfMass,
		float InMinFlapAngle, float InMaxFlapAngle,
		float InStartFlapPosition, float InEndFlapPosition,
		UDataTable* ProfileAerodynamicTable, bool IsMirrorSurface,
		EFlapType SurfaceFlapType, UControlSurfaceSC* ControlSurfaceSC);

	FAerodynamicForce CalculateForcesOnSubSurface(FVector LinearVelocity, FVector AngularVelocity,
		FVector GlobalCenterOfMassInWorld, FVector AirflowDirection, FControlInputState ControlState);

private:
	TArray<FVector> Start3DProfile;
	TArray<FVector> End3DProfile;
	FChord StartChord;
	FChord EndChord;
	float MinFlapAngle;
	float MaxFlapAngle;
	float StartFlapPosition;
	float EndFlapPosition;

	UDataTable* AerodynamicTable;

	static constexpr float AirDensity = 1.225f;
	const FVector Wind = FVector();
	bool IsMirror = false;
	EFlapType FlapType;
	UControlSurfaceSC* ControlSurface;

	FVector CenterOfPressure;
	float SurfaceArea;
	float DistanceToCenterOfMass;
};
