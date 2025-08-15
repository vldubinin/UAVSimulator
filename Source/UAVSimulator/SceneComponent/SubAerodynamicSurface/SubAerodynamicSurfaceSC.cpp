// Fill out your copyright notice in the Description page of Project Settings.


#include "SubAerodynamicSurfaceSC.h"

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
	SurfaceArea = 0.f;
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

void USubAerodynamicSurfaceSC::InitComponent(TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile, FName SurfaceName, float CenterOfPressureOffset/*, FVector GlobalSurfaceCenterOfMass*/ )
{
	Start3DProfile = InStart3DProfile;
	End3DProfile = InEnd3DProfile;
	StartChord = AerodynamicUtil::FindChord(Start3DProfile);
	EndChord = AerodynamicUtil::FindChord(End3DProfile);

	CenterOfPressure = FindCenterOfPressure(CenterOfPressureOffset);
	SurfaceArea = CalculateQuadSurfaceArea();

	DrawSurface(SurfaceName);
	DrawSplineCrosshairs(CenterOfPressure, SurfaceName);
	DrawText(FString::Printf(TEXT("Area: %f sm²"), SurfaceArea), CenterOfPressure, FVector(0.f, 0.f, 50.f), FRotator(90.f, 180.f, 0.f), FColor::Red, SurfaceName);
	
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
}

void USubAerodynamicSurfaceSC::DrawText(FString Text, FVector Point, FVector Offset, FRotator Rotator, FColor Color, FName SplineName)
{
	UTextRenderComponent* AreaTextRenderComponent = NewObject<UTextRenderComponent>(this, FName(*FString::Printf(TEXT("Area_Text_%s"), *SplineName.ToString())));
	AreaTextRenderComponent->RegisterComponent();
	AreaTextRenderComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	AreaTextRenderComponent->SetVerticalAlignment(EVRTA_TextCenter);
	AreaTextRenderComponent->SetHorizontalAlignment(EHTA_Center);
	AreaTextRenderComponent->SetWorldSize(10.0f);
	AreaTextRenderComponent->SetTextRenderColor(Color);
	AreaTextRenderComponent->SetWorldLocationAndRotation(Point + Offset, Rotator);
	AreaTextRenderComponent->SetText(FText::FromString(*Text));
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

float USubAerodynamicSurfaceSC::CalculateQuadSurfaceArea()
{
	const FVector Edge1 = StartChord.EndPoint - StartChord.StartPoint;
	const FVector Edge2 = EndChord.EndPoint - StartChord.StartPoint;
	const float Area1 = FVector::CrossProduct(Edge1, Edge2).Size() * 0.5f;

	const FVector Edge3 = EndChord.StartPoint - StartChord.StartPoint;
	const float Area2 = FVector::CrossProduct(Edge2, Edge3).Size() * 0.5f;

	return Area1 + Area2;
}