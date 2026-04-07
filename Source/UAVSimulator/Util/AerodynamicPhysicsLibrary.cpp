#include "AerodynamicPhysicsLibrary.h"
#include "UAVSimulator/UAVSimulator.h"

float UAerodynamicPhysicsLibrary::CalculateAngleOfAttack(FVector WorldAirVelocity, FVector AverageChordDirection, FVector SurfaceUpVector)
{
	float FlowAlongChord = FVector::DotProduct(WorldAirVelocity, AverageChordDirection);
	float FlowNormalToChord = FVector::DotProduct(WorldAirVelocity, SurfaceUpVector);
	float AngleOfAttackRad = FMath::Atan2(FlowNormalToChord, -FlowAlongChord);
	return FMath::RadiansToDegrees(AngleOfAttackRad);
}

float UAerodynamicPhysicsLibrary::CalculateLift(float AoA, float DynamicPressure, float SurfaceArea, FAerodynamicProfileRow* Profile)
{
	if (!Profile) { UE_LOG(LogUAV, Warning, TEXT("CalculateLift: null profile")); return 0.f; }
	float Cl = Profile->ClVsAoA.GetRichCurve()->Eval(AoA) / 100.f;
	UE_LOG(LogUAV, Verbose, TEXT("Cl: %f"), Cl);
	return Cl * DynamicPressure * (SurfaceArea / 10000.f);
}

float UAerodynamicPhysicsLibrary::CalculateDrag(float AoA, float DynamicPressure, float SurfaceArea, FAerodynamicProfileRow* Profile)
{
	if (!Profile) { UE_LOG(LogUAV, Warning, TEXT("CalculateDrag: null profile")); return 0.f; }
	float Cd = Profile->CdVsAoA.GetRichCurve()->Eval(AoA) / 100.f;
	UE_LOG(LogUAV, Verbose, TEXT("Cd: %f"), Cd);
	return Cd * DynamicPressure * (SurfaceArea / 10000.f);
}

float UAerodynamicPhysicsLibrary::CalculateTorque(float AoA, float DynamicPressure, float SurfaceArea, float ChordLength, FAerodynamicProfileRow* Profile)
{
	if (!Profile) { UE_LOG(LogUAV, Warning, TEXT("CalculateTorque: null profile")); return 0.f; }
	float Cm = Profile->CmVsAoA.GetRichCurve()->Eval(AoA) / 10000.f;
	UE_LOG(LogUAV, Verbose, TEXT("Cm: %f"), Cm);
	return Cm * DynamicPressure * (SurfaceArea / 10000.f) * ChordLength;
}

float UAerodynamicPhysicsLibrary::VelocityToMetersPerSecond(FVector WorldAirVelocity)
{
	return WorldAirVelocity.Size() / 100.0f;
}

float UAerodynamicPhysicsLibrary::NewtonsToKiloCentimeter(float Newtons)
{
	return Newtons * 100.f;
}

float UAerodynamicPhysicsLibrary::CalculateAverageChordLength(FChord FirstChord, FChord SecondChord)
{
	return ((FVector::Dist(FirstChord.StartPoint, FirstChord.EndPoint) / 100.0f)
		  + (FVector::Dist(SecondChord.StartPoint, SecondChord.EndPoint) / 100.0f)) / 2.f;
}

FVector UAerodynamicPhysicsLibrary::CalculateLiftDirection(FVector WorldAirVelocity, FVector SpanDirection)
{
	FVector DragDirection = WorldAirVelocity.GetSafeNormal();
	return FVector::CrossProduct(SpanDirection, DragDirection);
}

FVector UAerodynamicPhysicsLibrary::FindCenterOfPressure(FChord StartChord, FChord EndChord, float PercentageOffset)
{
	FVector FrontMidpoint = (StartChord.StartPoint + EndChord.StartPoint) / 2.0f;
	FVector BackMidpoint  = (StartChord.EndPoint   + EndChord.EndPoint)   / 2.0f;
	float Alpha = FMath::Clamp(PercentageOffset / 100.0f, 0.0f, 1.0f);
	return FMath::Lerp(FrontMidpoint, BackMidpoint, Alpha);
}

float UAerodynamicPhysicsLibrary::CalculateQuadSurfaceArea(FChord StartChord, FChord EndChord)
{
	const FVector Edge1 = StartChord.EndPoint - StartChord.StartPoint;
	const FVector Edge2 = EndChord.EndPoint   - StartChord.StartPoint;
	const float Area1 = FVector::CrossProduct(Edge1, Edge2).Size() * 0.5f;

	const FVector Edge3 = EndChord.StartPoint - StartChord.StartPoint;
	const float Area2 = FVector::CrossProduct(Edge2, Edge3).Size() * 0.5f;

	return Area1 + Area2;
}

FVector UAerodynamicPhysicsLibrary::GetPointOnLineAtPercentage(FVector StartPoint, FVector EndPoint, float Alpha)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	return FMath::Lerp(StartPoint, EndPoint, ClampedAlpha);
}
