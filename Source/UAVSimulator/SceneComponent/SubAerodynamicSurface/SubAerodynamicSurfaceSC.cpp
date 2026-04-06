// Fill out your copyright notice in the Description page of Project Settings.

#include "SubAerodynamicSurfaceSC.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/AerodynamicPhysicsLibrary.h"
#include "UAVSimulator/Util/AerodynamicProfileLookup.h"
#include "UAVSimulator/Util/AerodynamicDebugRenderer.h"
#include "UAVSimulator/Util/ControlInputMapper.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "DrawDebugHelpers.h"

USubAerodynamicSurfaceSC::USubAerodynamicSurfaceSC()
{
	PrimaryComponentTick.bCanEverTick = true;

	Start3DProfile = TArray<FVector>();
	End3DProfile   = TArray<FVector>();
	StartChord     = FChord();
	EndChord       = FChord();
	CenterOfPressure         = FVector();
	SurfaceArea              = 0.f;
	DistanceToCenterOfMass   = 0.f;
}

void USubAerodynamicSurfaceSC::InitComponent(
	TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile,
	FName SurfaceName, float CenterOfPressureOffset, FVector GlobalSurfaceCenterOfMass,
	float InMinFlapAngle, float InMaxFlapAngle,
	float InStartFlapPosition, float InEndFlapPosition,
	UDataTable* ProfileAerodynamicTable, bool IsMirrorSurface,
	EFlapType SurfaceFlapType, UControlSurfaceSC* ControlSurfaceSC)
{
	Start3DProfile = InStart3DProfile;
	End3DProfile   = InEnd3DProfile;
	StartChord = AerodynamicUtil::FindChord(Start3DProfile);
	EndChord   = AerodynamicUtil::FindChord(End3DProfile);

	AerodynamicTable = ProfileAerodynamicTable;

	CenterOfPressure = UAerodynamicPhysicsLibrary::FindCenterOfPressure(StartChord, EndChord, CenterOfPressureOffset);
	SurfaceArea      = UAerodynamicPhysicsLibrary::CalculateQuadSurfaceArea(StartChord, EndChord);

	const FTransform& Transform = GetComponentTransform();
	FVector WorldCoP = Transform.TransformPosition(CenterOfPressure);
	DistanceToCenterOfMass = FVector::Dist(GlobalSurfaceCenterOfMass, WorldCoP);

	MinFlapAngle      = InMinFlapAngle;
	MaxFlapAngle      = InMaxFlapAngle;
	StartFlapPosition = InStartFlapPosition;
	EndFlapPosition   = InEndFlapPosition;

	IsMirror       = IsMirrorSurface;
	FlapType       = SurfaceFlapType;
	ControlSurface = ControlSurfaceSC;

	// Editor visualization
	AerodynamicDebugRenderer::DrawSurface(this, StartChord, EndChord, Start3DProfile, End3DProfile, SurfaceName);
	AerodynamicDebugRenderer::DrawFlap(this, StartChord, EndChord, StartFlapPosition, EndFlapPosition, MinFlapAngle, MaxFlapAngle, SurfaceName);
	AerodynamicDebugRenderer::DrawCrosshairs(this, CenterOfPressure, SurfaceName);
	AerodynamicDebugRenderer::DrawLabel(this,
		FString::Printf(TEXT("Area: %f sm²"), SurfaceArea),
		CenterOfPressure, FVector(0.f, 0.f, 50.f), FRotator(90.f, 180.f, 0.f), FColor::Red,
		FName(FString::Printf(TEXT("Area_%s"), *SurfaceName.ToString())));
	AerodynamicDebugRenderer::DrawLabel(this,
		FString::Printf(TEXT("Dist to CoM: %f"), DistanceToCenterOfMass),
		CenterOfPressure, FVector(-20.f, 0.f, 50.f), FRotator(90.f, 180.f, 0.f), FColor::Red,
		FName(FString::Printf(TEXT("Distance_%s"), *SurfaceName.ToString())));
}

FAerodynamicForce USubAerodynamicSurfaceSC::CalculateForcesOnSubSurface(
	FVector LinearVelocity, FVector AngularVelocity,
	FVector GlobalCenterOfMassInWorld, FVector AirflowDirection,
	FControlInputState ControlState)
{
	const FTransform& Transform = GetComponentTransform();

	FChord StartChordInWorld = FChord(
		Transform.TransformPosition(StartChord.StartPoint),
		Transform.TransformPosition(StartChord.EndPoint));
	FChord EndChordInWorld = FChord(
		Transform.TransformPosition(EndChord.StartPoint),
		Transform.TransformPosition(EndChord.EndPoint));

	FVector CenterOfPressureInWorld = Transform.TransformPosition(CenterOfPressure);

	FVector StartChordDirection = (StartChordInWorld.StartPoint - StartChordInWorld.EndPoint).GetSafeNormal();
	FVector EndChordDirection   = (EndChordInWorld.StartPoint   - EndChordInWorld.EndPoint).GetSafeNormal();
	FVector AverageChordDirection = (StartChordDirection + EndChordDirection).GetSafeNormal();

	float AverageChordLength = UAerodynamicPhysicsLibrary::CalculateAverageChordLength(StartChordInWorld, EndChordInWorld);

	FVector RelativePosition    = CenterOfPressureInWorld - GlobalCenterOfMassInWorld;
	FVector RotationalVelocity  = FVector::CrossProduct(AngularVelocity, RelativePosition);
	FVector WorldAirVelocity    = -LinearVelocity + Wind - RotationalVelocity;

	float Speed          = UAerodynamicPhysicsLibrary::VelocityToMetersPerSecond(WorldAirVelocity);
	float AoA            = UAerodynamicPhysicsLibrary::CalculateAngleOfAttack(WorldAirVelocity, AverageChordDirection, GetUpVector());
	float DynamicPressure = 0.5f * AirDensity * (Speed * Speed);

	int32 FlapAngle = (int32)ControlInputMapper::ResolveFlapAngle(FlapType, IsMirror, MinFlapAngle, MaxFlapAngle, ControlState);

	if (ControlSurface)
	{
		ControlSurface->Move((float)FlapAngle);
	}

	FAerodynamicProfileRow* Profile = AerodynamicProfileLookup::FindProfile(AerodynamicTable, FlapAngle);

	float LiftPower  = UAerodynamicPhysicsLibrary::CalculateLift(AoA, DynamicPressure, SurfaceArea, Profile);
	float DragPower  = UAerodynamicPhysicsLibrary::CalculateDrag(AoA, DynamicPressure, SurfaceArea, Profile);
	float TorquePower = UAerodynamicPhysicsLibrary::CalculateTorque(AoA, DynamicPressure, SurfaceArea, AverageChordLength, Profile);

	FVector LiftForce  = UAerodynamicPhysicsLibrary::CalculateLiftDirection(WorldAirVelocity, GetRightVector())
		* UAerodynamicPhysicsLibrary::NewtonsToKiloCentimeter(LiftPower);
	FVector DragForce  = WorldAirVelocity.GetSafeNormal()
		* UAerodynamicPhysicsLibrary::NewtonsToKiloCentimeter(DragPower);
	FVector TorqueForce = GetRightVector() * (TorquePower * 10000.0f);

	FAerodynamicForce Result = FAerodynamicForce(LiftForce, DragForce, TorqueForce, RelativePosition);

	// Runtime debug visualization
	FVector EndLocation = CenterOfPressureInWorld + Result.PositionalForce;
	FVector MidPoint = FMath::Lerp(CenterOfPressureInWorld, EndLocation, 0.5f);
	DrawDebugDirectionalArrow(GetWorld(), CenterOfPressureInWorld, MidPoint, 25.0f, FColor::Green, false, -1.f, 0, 5.0f);

	return Result;
}
