// Fill out your copyright notice in the Description page of Project Settings.


#include "AerodynamicSurfaceSC.h"

// Sets default values for this component's properties
UAerodynamicSurfaceSC::UAerodynamicSurfaceSC()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UAerodynamicSurfaceSC::BeginPlay()
{
	Super::BeginPlay();
}


// Called every frame
void UAerodynamicSurfaceSC::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

}


void UAerodynamicSurfaceSC::OnConstruction()
{
	if (!Enable) {
		UE_LOG(LogTemp, Warning, TEXT("Surface is disabled."));
		return;
	}

	BuildSubsurfaces(1);
	if (Mirror) {
		BuildSubsurfaces(-1);
	}
}


void UAerodynamicSurfaceSC::BuildSubsurfaces(int32 Direction)
{
	for (USubAerodynamicSurfaceSC* SubSurface : SubSurfaces)
	{
		if (SubSurface) SubSurface->DestroyComponent();
	}
	SubSurfaces.Empty();

	TArray<FAerodynamicProfileStructure> Points = AerodynamicUtil::NormalizePoints(GetPoints());
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
		USubAerodynamicSurfaceSC* CustomSceneComponent = NewObject<USubAerodynamicSurfaceSC>(this, ComponentName);
		if (CustomSceneComponent)
		{
			CustomSceneComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			CustomSceneComponent->RegisterComponent();
			CustomSceneComponent->InitComponent(AerodynamicUtil::ConvertToWorldCoordinates(this, Start3DProfile), AerodynamicUtil::ConvertToWorldCoordinates(this, End3DProfile), ComponentName, AerodynamicCenterOffsetPercent);
			SubSurfaces.Add(CustomSceneComponent);
		}
	}
}

TArray<FAerodynamicProfileStructure> UAerodynamicSurfaceSC::GetPoints()
{
	TArray<FAerodynamicProfileStructure> ResultPoints;
	if (AerodynamicProfile == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("Missing Wing Profile."));
		return ResultPoints;
	}
	TArray<FAerodynamicProfileStructure*> RowPoints;
	UDataTable* Profile = AerodynamicProfile->Profile;
	if (Profile == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("Missing Data table configuration for Wing Profile."));
		return ResultPoints;
	}

	Profile->GetAllRows("Get all profile points from Data Table.", RowPoints);
	for (FAerodynamicProfileStructure* Point : RowPoints)
	{
		if (Point)
		{
			ResultPoints.Add(*Point);
		}
	}
	return ResultPoints;
}
