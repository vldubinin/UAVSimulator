// Fill out your copyright notice in the Description page of Project Settings.


#include "AerodynamicSurfaceSC.h"



// Sets default values for this component's properties
UAerodynamicSurfaceSC::UAerodynamicSurfaceSC()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}


void UAerodynamicSurfaceSC::OnConstruction(FVector CenterOfMass)
{
	if (!Enable) {
		UE_LOG(LogTemp, Warning, TEXT("Surface is disabled."));
		return;
	}

	DestroySubsurfaces();

	BuildSubsurfaces(CenterOfMass, 1);
	if (Mirror) {
		BuildSubsurfaces(CenterOfMass, -1);
	}
}

AerodynamicForce UAerodynamicSurfaceSC::CalculateForcesOnSurface(FVector CenterOfMass, FVector LinearVelocity, FVector AngularVelocity, FVector AirflowDirection)
{
	AerodynamicForce TotalAerodynamicForceForAllSubSurfaces;
	for (USubAerodynamicSurfaceSC* SubSurface : SubSurfaces)
	{
		AerodynamicForce SubSurfaceForces = SubSurface->CalculateForcesOnSubSurface(LinearVelocity, AngularVelocity, CenterOfMass, AirflowDirection);
		TotalAerodynamicForceForAllSubSurfaces.PositionalForce += SubSurfaceForces.PositionalForce;
		TotalAerodynamicForceForAllSubSurfaces.RotationalForce += SubSurfaceForces.RotationalForce;
	}
	return TotalAerodynamicForceForAllSubSurfaces;
}

void UAerodynamicSurfaceSC::DestroySubsurfaces() {
	for (USubAerodynamicSurfaceSC* SubSurface : SubSurfaces)
	{
		if (SubSurface) SubSurface->DestroyComponent();
	}
	SubSurfaces.Empty();
}

void UAerodynamicSurfaceSC::BuildSubsurfaces(FVector CenterOfMass, int32 Direction)
{
	TArray<FAirfoilPointData> Points = AerodynamicUtil::NormalizePoints(GetPoints());
	if (Points.Num() == 0) {
		UE_LOG(LogTemp, Warning, TEXT("Profile is missing."));
		return;
	}
	Chord ProfileChord = AerodynamicUtil::FindChord(Points);
	if (FMath::IsNearlyZero(ProfileChord.Length))
	{
		UE_LOG(LogTemp, Warning, TEXT("ScaleProfileByChord: Current distance is zero, cannot scale."));
		return;
	}

	if (SurfaceForm.Num() < 2)
	{
		return;
	}

	FVector GlobalOffset = FVector::ZeroVector;
	for (int32 i = 0; i < SurfaceForm.Num() - 1; i++)
	{
		const FAerodynamicSurfaceStructure& StartConfig = SurfaceForm[i];
		const FAerodynamicSurfaceStructure& EndConfig = SurfaceForm[i + 1];
		TArray<FVector> Start3DProfile = AerodynamicUtil::ConvertTo3DPoints(Points, ProfileChord.Length, StartConfig.ChordSize, GlobalOffset);
		GlobalOffset += FVector(EndConfig.Offset.X, EndConfig.Offset.Y * Direction, EndConfig.Offset.Z);
		TArray<FVector> End3DProfile = AerodynamicUtil::ConvertTo3DPoints(Points, ProfileChord.Length, EndConfig.ChordSize, GlobalOffset);

		FName ComponentName = FName(*FString::Printf(TEXT("Sub_%s__%d_dir_%d"), *this->GetName(), i, Direction));
		USubAerodynamicSurfaceSC* SubAerodynamicSurface = NewObject<USubAerodynamicSurfaceSC>(this, ComponentName);
		if (SubAerodynamicSurface)
		{
			SubAerodynamicSurface->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			SubAerodynamicSurface->RegisterComponent();
			SubAerodynamicSurface->InitComponent(Start3DProfile, End3DProfile, ComponentName, AerodynamicCenterOffsetPercent, CenterOfMass, StartConfig.StartFlapPosition, StartConfig.EndFlapPosition, StartConfig.AerodynamicTable);
			SubSurfaces.Add(SubAerodynamicSurface);
		}
	}
}

TArray<FAirfoilPointData> UAerodynamicSurfaceSC::GetPoints()
{
	TArray<FAirfoilPointData> ResultPoints;
	TArray<FAirfoilPointData*> RowPoints;
	if (Profile == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("Missing Data table configuration for Wing Profile."));
		return ResultPoints;
	}

	Profile->GetAllRows("Get all profile points from Data Table.", RowPoints);
	for (FAirfoilPointData* Point : RowPoints)
	{
		if (Point)
		{
			ResultPoints.Add(*Point);
		}
	}
	return ResultPoints;
}

