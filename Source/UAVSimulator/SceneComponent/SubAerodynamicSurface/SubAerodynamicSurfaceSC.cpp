// Fill out your copyright notice in the Description page of Project Settings.


#include "SubAerodynamicSurfaceSC.h"
#include <Runtime/Engine/Public/DrawDebugHelpers.h>

// Sets default values for this component's properties
USubAerodynamicSurfaceSC::USubAerodynamicSurfaceSC()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	Start3DProfile = TArray<FVector>();
	End3DProfile = TArray<FVector>();
	StartChord = Chord();
	EndChord = Chord();
	CenterOfPressure = FVector();
}


// Called when the game starts
void USubAerodynamicSurfaceSC::BeginPlay()
{
	Super::BeginPlay();
}


// Called every frame
void USubAerodynamicSurfaceSC::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void USubAerodynamicSurfaceSC::InitComponent(TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile, FName SurfaceName, float CenterOfPressureOffset)
{
	Start3DProfile = AerodynamicUtil::ConvertToWorldCoordinates(this, InStart3DProfile);
	End3DProfile = AerodynamicUtil::ConvertToWorldCoordinates(this, InEnd3DProfile);
	StartChord = AerodynamicUtil::FindChord(Start3DProfile);
	EndChord = AerodynamicUtil::FindChord(End3DProfile);

	FVector MidPoint = (StartChord.StartPoint + EndChord.StartPoint) / 2.0f;
	CenterOfPressure = FindCenterOfPressure(CenterOfPressureOffset);

	DrawSurface(SurfaceName);
	
}

void USubAerodynamicSurfaceSC::DrawSurface(FName SplineName)
{
	DrawSpline(Start3DProfile, FName(*FString::Printf(TEXT("Start_Spline_%s"), *SplineName.ToString())));
	DrawSpline(End3DProfile, FName(*FString::Printf(TEXT("End_Spline_%s"), *SplineName.ToString())));
	TArray<FVector> TopBorder = TArray<FVector>();
	TopBorder.Add(StartChord.StartPoint);
	TopBorder.Add(EndChord.StartPoint);
	DrawSpline(TopBorder, FName(*FString::Printf(TEXT("Top_Border_Spline_%s"), *SplineName.ToString())));

	TArray<FVector> BottomBorder = TArray<FVector>();
	BottomBorder.Add(StartChord.EndPoint);
	BottomBorder.Add(EndChord.EndPoint);
	DrawSpline(BottomBorder, FName(*FString::Printf(TEXT("Bottom_Border_Spline_%s"), *SplineName.ToString())));

	DrawSplineCrosshairs(CenterOfPressure, SplineName);
	//DrawDebugCrosshairs(GetWorld(), CenterOfPressure, FRotator::ZeroRotator, 250.0f, FColor::Magenta, false, 500, 0);
}

void USubAerodynamicSurfaceSC::DrawSpline(TArray<FVector> Points, FName SplineName)
{
	USplineComponent* ProfileSplineComponent = NewObject<USplineComponent>(this, SplineName);
	if (!ProfileSplineComponent)
	{
		return;
	}

	ProfileSplineComponent->RegisterComponent();
	ProfileSplineComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);

	ProfileSplineComponent->ClearSplinePoints();
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		ProfileSplineComponent->AddSplinePoint(Points[i], ESplineCoordinateSpace::World);
	}
	ProfileSplineComponent->UpdateSpline();
}

void USubAerodynamicSurfaceSC::DrawSplineCrosshairs(FVector Point, FName SplineName)
{
	TArray<FVector> CrosshairsPoints = TArray<FVector>();
	CrosshairsPoints.Add(Point + FVector(50.f, 0.f, 0.f));
	CrosshairsPoints.Add(Point + FVector(-50.f, 0.f, 0.f));
	CrosshairsPoints.Add(Point);
	CrosshairsPoints.Add(Point + FVector(0.f, 50.f, 0.f));
	CrosshairsPoints.Add(Point + FVector(0.f, -50.f, 0.f));
	CrosshairsPoints.Add(Point);
	CrosshairsPoints.Add(Point + FVector(0.f, 0.f, 50.f));
	CrosshairsPoints.Add(Point + FVector(0.f, 0.f, -50.f));
	DrawSpline(CrosshairsPoints, FName(*FString::Printf(TEXT("Crosshairs_Spline_%s"), *SplineName.ToString())));
}

FVector USubAerodynamicSurfaceSC::FindCenterOfPressure(float PercentageOffset)
{
	FVector FrontChordMidpoint = (StartChord.StartPoint + EndChord.StartPoint) / 2.0f;
	FVector BackChordMidpoint = (StartChord.EndPoint + EndChord.EndPoint) / 2.0f;
	float Alpha = FMath::Clamp(PercentageOffset / 100.0f, 0.0f, 1.0f);

	return FMath::Lerp(FrontChordMidpoint, BackChordMidpoint, Alpha);
}