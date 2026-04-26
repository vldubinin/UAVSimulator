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

	// Хорди визначаються за екстремумами X профілю (передня і задня кромки)
	StartChord = AerodynamicUtil::FindChord(Start3DProfile);
	EndChord   = AerodynamicUtil::FindChord(End3DProfile);

	// AerodynamicTable може бути nullptr — в такому випадку сили розраховуватимуться як нуль
	AerodynamicTable = ProfileAerodynamicTable;

	// Центр тиску — точка прикладання аеродинамічних сил (локальний простір)
	CenterOfPressure = UAerodynamicPhysicsLibrary::FindCenterOfPressure(StartChord, EndChord, CenterOfPressureOffset);
	// Площа чотирикутної секції поверхні (в cm²)
	SurfaceArea      = UAerodynamicPhysicsLibrary::CalculateQuadSurfaceArea(StartChord, EndChord);

	// Відстань від центру тиску до центру мас потрібна для розрахунку моменту
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

	// Відображення геометрії поверхні у редакторі (незалежно від наявності AerodynamicTable)
	AerodynamicDebugRenderer::DrawSurface(this, StartChord, EndChord, Start3DProfile, End3DProfile, SurfaceName);
	AerodynamicDebugRenderer::DrawFlap(this, StartChord, EndChord, StartFlapPosition, EndFlapPosition, MinFlapAngle, MaxFlapAngle, SurfaceName);
	AerodynamicDebugRenderer::DrawCrosshairs(this, CenterOfPressure, SurfaceName);
	// Мітка площі поверхні
	AerodynamicDebugRenderer::DrawLabel(this,
		FString::Printf(TEXT("Area: %f sm²"), SurfaceArea),
		CenterOfPressure, FVector(0.f, 0.f, 50.f), FRotator(90.f, 180.f, 0.f), FColor::Red,
		FName(FString::Printf(TEXT("Area_%s"), *SurfaceName.ToString())));
	// Мітка відстані до центру мас
	AerodynamicDebugRenderer::DrawLabel(this,
		FString::Printf(TEXT("Dist to CoM: %f"), DistanceToCenterOfMass),
		CenterOfPressure, FVector(-20.f, 0.f, 50.f), FRotator(90.f, 180.f, 0.f), FColor::Red,
		FName(FString::Printf(TEXT("Distance_%s"), *SurfaceName.ToString())));
}

FAerodynamicForce USubAerodynamicSurfaceSC::CalculateForcesOnSubSurface(
	FVector LinearVelocity, FVector AngularVelocity,
	FVector GlobalCenterOfMassInWorld, FVector AirflowDirection,
	FControlInputState ControlState, bool bVisualizeForces)
{
	const FTransform& Transform = GetComponentTransform();

	// Переводимо збережені локальні хорди у світовий простір для поточного кадру
	FChord StartChordInWorld = FChord(
		Transform.TransformPosition(StartChord.StartPoint),
		Transform.TransformPosition(StartChord.EndPoint));
	FChord EndChordInWorld = FChord(
		Transform.TransformPosition(EndChord.StartPoint),
		Transform.TransformPosition(EndChord.EndPoint));

	FVector CenterOfPressureInWorld = Transform.TransformPosition(CenterOfPressure);

	// Середній напрямок хорди: усереднення напрямків двох крайніх хорд секції
	FVector StartChordDirection = (StartChordInWorld.StartPoint - StartChordInWorld.EndPoint).GetSafeNormal();
	FVector EndChordDirection   = (EndChordInWorld.StartPoint   - EndChordInWorld.EndPoint).GetSafeNormal();
	FVector AverageChordDirection = (StartChordDirection + EndChordDirection).GetSafeNormal();

	float AverageChordLength = UAerodynamicPhysicsLibrary::CalculateAverageChordLength(StartChordInWorld, EndChordInWorld);

	// Вектор відносного положення центру тиску від центру мас (для розрахунку моменту)
	FVector RelativePosition    = CenterOfPressureInWorld - GlobalCenterOfMassInWorld;
	// Швидкість від обертального руху секції відносно центру мас (ω × r)
	FVector RotationalVelocity  = FVector::CrossProduct(AngularVelocity, RelativePosition);
	// Результуюча швидкість повітря відносно поверхні = -V_linear + Wind - V_rotational
	FVector WorldAirVelocity    = -LinearVelocity + Wind - RotationalVelocity;

	float Speed           = UAerodynamicPhysicsLibrary::VelocityToMetersPerSecond(WorldAirVelocity);
	float AoA             = UAerodynamicPhysicsLibrary::CalculateAngleOfAttack(WorldAirVelocity, AverageChordDirection, GetUpVector());
	float DynamicPressure = 0.5f * AirDensity * (Speed * Speed);  // q = 0.5 * ρ * V² (Па)

	// Визначаємо кут відхилення закрилка та переміщуємо фізичний компонент-закрилок
	int32 FlapAngle = (int32)ControlInputMapper::ResolveFlapAngle(FlapType, IsMirror, MinFlapAngle, MaxFlapAngle, ControlState);
	if (ControlSurface)
	{
		ControlSurface->Move((float)FlapAngle);
	}

	// Знаходимо профіль для поточного кута закрилка (nullptr якщо таблиця не задана)
	FAerodynamicProfileRow* Profile = AerodynamicProfileLookup::FindProfile(AerodynamicTable, FlapAngle);

	// Розраховуємо сили в Ньютонах, конвертуємо у внутрішні одиниці Unreal (kg·cm/s²)
	float LiftPower   = UAerodynamicPhysicsLibrary::CalculateLift(AoA, DynamicPressure, SurfaceArea, Profile);
	float DragPower   = UAerodynamicPhysicsLibrary::CalculateDrag(AoA, DynamicPressure, SurfaceArea, Profile);
	float TorquePower = UAerodynamicPhysicsLibrary::CalculateTorque(AoA, DynamicPressure, SurfaceArea, AverageChordLength, Profile);

	// Підйомна сила — перпендикулярна до потоку і до осі розмаху
	FVector LiftForce  = UAerodynamicPhysicsLibrary::CalculateLiftDirection(WorldAirVelocity, GetRightVector())
		* UAerodynamicPhysicsLibrary::NewtonsToKiloCentimeter(LiftPower);
	// Сила опору — протилежна до напрямку руху
	FVector DragForce  = WorldAirVelocity.GetSafeNormal()
		* UAerodynamicPhysicsLibrary::NewtonsToKiloCentimeter(DragPower);
	// Тангажний момент навколо осі розмаху (конвертація Н·м → kg·cm²/s² через 10000)
	FVector TorqueForce = GetRightVector() * (TorquePower * 10000.0f);

	FAerodynamicForce Result = FAerodynamicForce(LiftForce, DragForce, TorqueForce, RelativePosition);

	// Відображення вектора результуючої сили під час гри (зелена стрілка)
	FVector EndLocation = CenterOfPressureInWorld + Result.PositionalForce;
	FVector MidPoint = FMath::Lerp(CenterOfPressureInWorld, EndLocation, 0.5f);
	
	if (bVisualizeForces) {
		DrawDebugDirectionalArrow(GetWorld(), CenterOfPressureInWorld, MidPoint, 25.0f, FColor::Green, false, -1.f, 0, 5.0f);
	}
	return Result;
}
