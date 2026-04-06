#include "AerodynamicDebugRenderer.h"
#include "Components/SplineComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"

void AerodynamicDebugRenderer::DrawSurface(USceneComponent* Parent,
	FChord StartChord, FChord EndChord,
	const TArray<FVector>& StartProfile, const TArray<FVector>& EndProfile,
	FName Name)
{
	DrawSpline(Parent, StartProfile, FName(*FString::Printf(TEXT("Start_Spline_%s"), *Name.ToString())));
	DrawSpline(Parent, EndProfile,   FName(*FString::Printf(TEXT("End_Spline_%s"),   *Name.ToString())));

	TArray<FVector> TopBorder    = { StartChord.StartPoint, EndChord.StartPoint };
	TArray<FVector> BottomBorder = { StartChord.EndPoint,   EndChord.EndPoint   };
	DrawSpline(Parent, TopBorder,    FName(*FString::Printf(TEXT("Top_Border_Spline_%s"),    *Name.ToString())));
	DrawSpline(Parent, BottomBorder, FName(*FString::Printf(TEXT("Bottom_Border_Spline_%s"), *Name.ToString())));
}

void AerodynamicDebugRenderer::DrawSpline(USceneComponent* Parent, const TArray<FVector>& Points, FName Name)
{
	if (!Parent) return;

	USplineComponent* Spline = NewObject<USplineComponent>(Parent, Name);
	if (!Spline) return;

	Spline->RegisterComponent();
	Spline->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
	Spline->ClearSplinePoints();
	for (const FVector& Pt : Points)
	{
		Spline->AddSplinePoint(Pt, ESplineCoordinateSpace::Local);
	}
	Spline->UpdateSpline();
}

void AerodynamicDebugRenderer::DrawFlap(USceneComponent* Parent,
	FChord StartChord, FChord EndChord,
	float StartFlapPosition, float EndFlapPosition,
	float MinFlapAngle, float MaxFlapAngle,
	FName Name)
{
	if (MinFlapAngle == 0.f || MaxFlapAngle == 0.f) return;

	TArray<FVector> FlapPoints = {
		GetPointOnLineAtPercentage(StartChord.StartPoint, StartChord.EndPoint, StartFlapPosition / 100.f),
		GetPointOnLineAtPercentage(EndChord.StartPoint,   EndChord.EndPoint,   EndFlapPosition   / 100.f)
	};
	DrawSpline(Parent, FlapPoints, FName(*FString::Printf(TEXT("Flap_%s"), *Name.ToString())));
}

void AerodynamicDebugRenderer::DrawCrosshairs(USceneComponent* Parent, FVector Point, FName Name)
{
	TArray<FVector> CrosshairsPoints = {
		Point + FVector( 50.f,   0.f,   0.f),
		Point + FVector(-50.f,   0.f,   0.f),
		Point,
		Point + FVector(  0.f,  50.f,   0.f),
		Point + FVector(  0.f, -50.f,   0.f),
		Point,
		Point + FVector(  0.f,   0.f,  50.f),
		Point + FVector(  0.f,   0.f, -50.f)
	};
	DrawSpline(Parent, CrosshairsPoints, FName(*FString::Printf(TEXT("Crosshairs_Spline_%s"), *Name.ToString())));
}

void AerodynamicDebugRenderer::DrawLabel(USceneComponent* Parent,
	FString Text, FVector Point, FVector Offset,
	FRotator Rotator, FColor Color, FName Name)
{
	if (!Parent) return;

	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Parent,
		FName(*FString::Printf(TEXT("Area_Text_%s"), *Name.ToString())));
	Label->RegisterComponent();
	Label->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(10.0f);
	Label->SetTextRenderColor(Color);
	Label->SetRelativeLocationAndRotation(Point + Offset, Rotator);
	Label->SetText(FText::FromString(*Text));
}

void AerodynamicDebugRenderer::DrawForceArrow(const UWorld* World,
	FVector Origin, FVector Direction,
	FColor Color, float Scale,
	float ArrowSize, float Thickness, float Duration)
{
	if (!World) return;
	FVector EndLocation = Origin + (Direction * Scale);
	DrawDebugDirectionalArrow(World, Origin, EndLocation, ArrowSize, Color, false, Duration, 0, Thickness);
}

FVector AerodynamicDebugRenderer::GetPointOnLineAtPercentage(FVector Start, FVector End, float Alpha)
{
	return FMath::Lerp(Start, End, FMath::Clamp(Alpha, 0.f, 1.f));
}
