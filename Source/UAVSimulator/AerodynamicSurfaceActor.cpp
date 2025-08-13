// Fill out your copyright notice in the Description page of Project Settings.


#include "AerodynamicSurfaceActor.h"


AAerodynamicSurfaceActor::AAerodynamicSurfaceActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	SetRootComponent(DefaultSceneRoot);
}

void AAerodynamicSurfaceActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (AerodynamicProfile == nullptr) {
		UE_LOG(LogTemp, Error, TEXT("Missing Wing Profile Data."));
		return;
	}

	TArray<SubSurface> SubSurfaces = BuildSubsurfaces(1);
	DrawSurface(SubSurfaces, TEXT("LEFT"));

	if (Mirror) {
		TArray<SubSurface> MirrorSubSurfaces = BuildSubsurfaces(-1);
		DrawSurface(MirrorSubSurfaces, TEXT("RIGHT"));
	}
 }

void AAerodynamicSurfaceActor::DrawProfile(TArray<FAerodynamicProfileStructure> Points, FVector Offset, FName SplineName)
{
	TArray<FVector> SplinePoints = AdaptTo(Points, Offset);
	DrawSpline(SplinePoints, SplineName);
}

TArray<SubSurface> AAerodynamicSurfaceActor::BuildSubsurfaces(int32 Direction)
{
	TArray<SubSurface> SubSurfaces;

	TArray<FAerodynamicProfileStructure> Points = NormalizePoints(GetPoints());
	Chord ProfileChord = AerodynamicUtil::FindChord(Points);
	if (FMath::IsNearlyZero(ProfileChord.Length))
	{
		UE_LOG(LogTemp, Warning, TEXT("ScaleProfileByChord: Current distance is zero, cannot scale."));
		return SubSurfaces;
	}

	if (SurfaceForm.Num() < 2)
	{
		return SubSurfaces;
	}

	SubSurfaces.Reserve(SurfaceForm.Num() - 1); 

	FVector GlobalOffset = FVector::ZeroVector;
	for (int32 i = 0; i < SurfaceForm.Num() - 1; i++)
	{
		//ToDo
		const FAerodynamicSurfaceStructure& StartConfig = SurfaceForm[i];
		const FAerodynamicSurfaceStructure& EndConfig = SurfaceForm[i + 1];
		TArray<FVector> Start3DProfile = ConvertTo3DPoints(Points, ProfileChord.Length, StartConfig.ChordSize, GlobalOffset);
		GlobalOffset += FVector(EndConfig.Offset.X, EndConfig.Offset.Y * Direction, EndConfig.Offset.Z);
		TArray<FVector> End3DProfile = ConvertTo3DPoints(Points, ProfileChord.Length, EndConfig.ChordSize, GlobalOffset);

		SubSurfaces.Add(SubSurface(Start3DProfile, End3DProfile));
	}

	return SubSurfaces;
}

void AAerodynamicSurfaceActor::DrawSurface(TArray<SubSurface> Surface, FName SplineName) {
	for (int32 i = 0; i < Surface.Num(); i++) {
		SubSurface SubSurf = Surface[i];
		DrawSpline(SubSurf.GetStart3DProfile(), FName(*FString::Printf(TEXT("Start_Spline_%s_%d"), *SplineName.ToString(), i)));
		DrawSpline(SubSurf.GetEnd3DProfile(), FName(*FString::Printf(TEXT("End_Spline_%s_%d"), *SplineName.ToString(), i)));

		TArray<FVector> TopBorder = TArray<FVector>();
		TopBorder.Add(SubSurf.GetStartChord().StartPoint);
		TopBorder.Add(SubSurf.GetEndChord().StartPoint);
		DrawSpline(TopBorder, FName(*FString::Printf(TEXT("Top_Border_Spline_%s_%d"), *SplineName.ToString(), i)));

		TArray<FVector> BottomBorder = TArray<FVector>();
		BottomBorder.Add(SubSurf.GetStartChord().EndPoint);
		BottomBorder.Add(SubSurf.GetEndChord().EndPoint);
		DrawSpline(BottomBorder, FName(*FString::Printf(TEXT("Bottom_Border_Spline_%s_%d"), *SplineName.ToString(), i)));
	}
}

TArray<FVector> AAerodynamicSurfaceActor::ConvertTo3DPoints(TArray<FAerodynamicProfileStructure> Profile, float ChordLength, float ExpectedChordLength, FVector Offset)
{
	TArray<FAerodynamicProfileStructure> ScaledProfilePoints = AerodynamicUtil::Scale(Profile, ExpectedChordLength / ChordLength);
	return AdaptTo(ScaledProfilePoints, Offset);
}

void AAerodynamicSurfaceActor::DrawChordConnections(TArray<Chord> Chords, FName SplineName)
{
	TArray<FVector> TopBorder = TArray<FVector>();
	TArray<FVector> BottomBorder = TArray<FVector>();
	for (int32 i = 0; i < Chords.Num(); i++) {
		Chord SurfaceBorder = Chords[i];
		TopBorder.Add(SurfaceBorder.StartPoint);
		BottomBorder.Add(SurfaceBorder.EndPoint);
	}

	DrawSpline(TopBorder, FName(*FString::Printf(TEXT("Top_Chord_Connection_%s"), *SplineName.ToString())));
	DrawSpline(BottomBorder, FName(*FString::Printf(TEXT("Bottom_Chord_Connection_%s"), *SplineName.ToString())));
}

TArray<FAerodynamicProfileStructure> AAerodynamicSurfaceActor::NormalizePoints(TArray<FAerodynamicProfileStructure> Points) {
	TArray<FAerodynamicProfileStructure> NormalizedPoints;
	for (FAerodynamicProfileStructure Point : Points) {
		FAerodynamicProfileStructure NormalizedPoint = FAerodynamicProfileStructure();
		NormalizedPoint.X = -Point.X;
		NormalizedPoint.Z = Point.Z;
		NormalizedPoints.Add(NormalizedPoint);
	}
	return NormalizedPoints;
}

TArray<FAerodynamicProfileStructure> AAerodynamicSurfaceActor::GetPoints()
{
	TArray<FAerodynamicProfileStructure> ResultPoints;
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

TArray<FVector> AAerodynamicSurfaceActor::AdaptTo(TArray<FAerodynamicProfileStructure> Points, FVector Offset)
{
	TArray<FVector> VectorsArray = TArray<FVector>();
	for (FAerodynamicProfileStructure Point : Points) {
		VectorsArray.Add(FVector(Point.X, 0.f, Point.Z) + Offset);
	}
	return VectorsArray;
}

void AAerodynamicSurfaceActor::DrawSpline(TArray<FVector> Points, FName SplineName)
{
	USplineComponent* ProfileSplineComponent = NewObject<USplineComponent>(this, SplineName);
	if (!ProfileSplineComponent)
	{
		return;
	}

	ProfileSplineComponent->RegisterComponent();
	ProfileSplineComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	ProfileSplineComponent->ClearSplinePoints();
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		ProfileSplineComponent->AddSplinePoint(Points[i], ESplineCoordinateSpace::Local);
	}
	ProfileSplineComponent->UpdateSpline();
}