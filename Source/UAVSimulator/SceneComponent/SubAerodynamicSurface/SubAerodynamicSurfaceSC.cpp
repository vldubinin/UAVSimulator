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
	DistanceToCenterOfMass = 0.f;
}

void USubAerodynamicSurfaceSC::InitComponent(TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile, 
	FName SurfaceName, float CenterOfPressureOffset, FVector GlobalSurfaceCenterOfMass, float InStartFlopPosition, 
	float InEndFlopPosition, UDataTable* ProfileAerodynamicTable, bool IsMirrorSurface, EFlapType SurfaceFlapType, 
	UControlSurfaceSC* ControlSurfaceSC)
{
	Start3DProfile = InStart3DProfile;
	End3DProfile = InEnd3DProfile;
	StartChord = AerodynamicUtil::FindChord(Start3DProfile);
	EndChord = AerodynamicUtil::FindChord(End3DProfile);

	AerodynamicTable = ProfileAerodynamicTable;

	CenterOfPressure = FindCenterOfPressure(CenterOfPressureOffset);
	SurfaceArea = CalculateQuadSurfaceArea();
	DistanceToCenterOfMass = FVector::Dist(GlobalSurfaceCenterOfMass, AerodynamicUtil::ConvertToWorldCoordinate(this, CenterOfPressure));
	StartFlopPosition = InStartFlopPosition;
	EndFlopPosition = InEndFlopPosition;

	IsMirror = IsMirrorSurface;
	FlapType = SurfaceFlapType;
	ControlSurface = ControlSurfaceSC;

	DrawSurface(SurfaceName);
	DrawFlop(SurfaceName);
	DrawSplineCrosshairs(CenterOfPressure, SurfaceName);
	DrawText(FString::Printf(TEXT("Area: %f sm²"), SurfaceArea), CenterOfPressure, FVector(0.f, 0.f, 50.f), FRotator(90.f, 180.f, 0.f), FColor::Red, FName(FString::Printf(TEXT("Area_%s"), *SurfaceName.ToString())));
	DrawText(FString::Printf(TEXT("Dist to CoM: %f"), DistanceToCenterOfMass), CenterOfPressure, FVector(-20.f, 0.f, 50.f), FRotator(90.f, 180.f, 0.f), FColor::Red, FName(FString::Printf(TEXT("Distance_%s"), *SurfaceName.ToString())));
	
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

void USubAerodynamicSurfaceSC::DrawFlop(FName SplineName)
{
	if (StartFlopPosition == 0.f || EndFlopPosition == 0.f) {
		return;
	}
	TArray<FVector> FlopPoints = TArray<FVector>();
	FlopPoints.Add(GetPointOnLineAtPercentage(StartChord.StartPoint, StartChord.EndPoint, StartFlopPosition / 100));
	FlopPoints.Add(GetPointOnLineAtPercentage(EndChord.StartPoint, EndChord.EndPoint, EndFlopPosition / 100));

	DrawSpline(FlopPoints, FName(*FString::Printf(TEXT("Flop_%s"), *SplineName.ToString())));
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
	AreaTextRenderComponent->SetRelativeLocationAndRotation(Point + Offset, Rotator);
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
		ProfileSplineComponent->AddSplinePoint(Points[i], ESplineCoordinateSpace::Local);
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

FVector USubAerodynamicSurfaceSC::GetPointOnLineAtPercentage(FVector StartPoint, FVector EndPoint, float Alpha)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	return FMath::Lerp(StartPoint, EndPoint, ClampedAlpha);
}

float USubAerodynamicSurfaceSC::CalculateFlapAngel(float StartFlopValue, float EndFlopValue, float InputSignal)
{
	float Mid = (StartFlopValue + EndFlopValue) * 0.5f;
	return (InputSignal < 0.0f)
		? FMath::Lerp(Mid, StartFlopValue, -InputSignal)
		: FMath::Lerp(Mid, EndFlopValue, InputSignal);
}

float USubAerodynamicSurfaceSC::GetFlapAngel(ControlInputState ControlState)
{
	if (FlapType == EFlapType::None)
	{
		UE_LOG(LogTemp, Warning, TEXT("None"));
		return 0;
	}

	if (FlapType == EFlapType::Aileron)
	{
		if (IsMirror) {
			float FlapAngel = CalculateFlapAngel(StartFlopPosition, EndFlopPosition, ControlState.RightAileronAngle);
			//UE_LOG(LogTemp, Warning, TEXT("Aileron R"));
			//UE_LOG(LogTemp, Warning, TEXT("Aileron R Angel: %f"), FlapAngel);
			return FlapAngel;
		}
		float FlapAngel = CalculateFlapAngel(StartFlopPosition, EndFlopPosition, ControlState.LeftAileronAngle);
		//UE_LOG(LogTemp, Warning, TEXT("Aileron L"));
		//UE_LOG(LogTemp, Warning, TEXT("Aileron L Angel: %f"), FlapAngel);
		return FlapAngel;
	}

	if (FlapType == EFlapType::Elevator)
	{
		if (IsMirror) {
			float FlapAngel = CalculateFlapAngel(StartFlopPosition, EndFlopPosition, ControlState.RightElevatorAngle);
			//UE_LOG(LogTemp, Warning, TEXT("Elevator R"));
			//UE_LOG(LogTemp, Warning, TEXT("Elevator R Angel: %f"), FlapAngel);
			return FlapAngel;
		}
		float FlapAngel = CalculateFlapAngel(StartFlopPosition, EndFlopPosition, ControlState.LeftElevatorAngle);
		//UE_LOG(LogTemp, Warning, TEXT("Elevator L"));
		//UE_LOG(LogTemp, Warning, TEXT("Elevator L Angel: %f"), FlapAngel);
		return FlapAngel;
	}

	if (FlapType == EFlapType::Rudder)
	{
		float FlapAngel = CalculateFlapAngel(StartFlopPosition, EndFlopPosition, ControlState.RudderAngle);
		//UE_LOG(LogTemp, Warning, TEXT("Rudder"));
		//UE_LOG(LogTemp, Warning, TEXT("Rudder Angel: %f"), FlapAngel);
		return FlapAngel;
	}
	return 0;
}

AerodynamicForce USubAerodynamicSurfaceSC::CalculateForcesOnSubSurface(FVector LinearVelocity, FVector AngularVelocity, FVector GlobalCenterOfMassInWorld, FVector AirflowDirection, ControlInputState ControlState)
{
	//UE_LOG(LogTemp, Warning, TEXT("###############################################"));
	Chord StartChordInWorld = AerodynamicUtil::ConvertChordToWorldCoordinate(this, StartChord);
	Chord EndChordInWorld = AerodynamicUtil::ConvertChordToWorldCoordinate(this, EndChord);

	FVector CenterOfPressureInWorld = AerodynamicUtil::ConvertToWorldCoordinate(this, CenterOfPressure);
	FVector StartChordDirection = (StartChordInWorld.StartPoint - StartChordInWorld.EndPoint).GetSafeNormal();
	FVector EndChordDirection = (EndChordInWorld.StartPoint - EndChordInWorld.EndPoint).GetSafeNormal();
	
	float AvarageChordLength = CalculateAvarageChordLength(StartChordInWorld, EndChordInWorld);
	FVector AverageChordDirection = (StartChordDirection + EndChordDirection).GetSafeNormal();
	FVector RelativePosition = AerodynamicUtil::ConvertToWorldCoordinate(this, CenterOfPressure) - GlobalCenterOfMassInWorld;
	FVector RotationalVelocity = FVector::CrossProduct(AngularVelocity, RelativePosition);
	FVector WorldAirVelocity = -LinearVelocity + Wind - RotationalVelocity;
	float Speed = ToSpeedInMetersPerSecond(WorldAirVelocity);

	//UE_LOG(LogTemp, Warning, TEXT("Speed: %f m/sec"), Speed);
	//UE_LOG(LogTemp, Warning, TEXT("WorldAirVelocity: %s"), *WorldAirVelocity.ToString());
	//UE_LOG(LogTemp, Warning, TEXT("AvarageChordLength: %f m." ), AvarageChordLength);
	//UE_LOG(LogTemp, Warning, TEXT("SurfaceArea: %f m²"), (SurfaceArea / 10000));
	//UE_LOG(LogTemp, Warning, TEXT("RelativePosition (CenterOfPressure - GlobalCenterOfMassInWorld): %s"), *RelativePosition.ToString());
	//UE_LOG(LogTemp, Warning, TEXT("RotationalVelocity (FVector::CrossProduct(AngularVelocity, RelativePosition)): %s"), *RotationalVelocity.ToString());

	float AngleOfAttackDeg = CalculateAngleOfAttackDeg(WorldAirVelocity, AverageChordDirection);
	float DynamicPressure = 0.5f * AirDensity * (Speed * Speed);

	int FlapAngle = GetFlapAngel(ControlState);

	if (ControlSurface != nullptr) {
		ControlSurface->Move(FlapAngle);
	}

	float LiftPower = CalculateLiftInNewtons(AngleOfAttackDeg, FlapAngle, DynamicPressure);
	float DragPower = CalculateDragInNewtons(AngleOfAttackDeg, FlapAngle, DynamicPressure);
	float TorquePower = CalculateTorqueInNewtons(AngleOfAttackDeg, FlapAngle, DynamicPressure, AvarageChordLength);

	FVector LiftForce = GetLiftDirection(WorldAirVelocity) * NewtonsToKiloCentimeter(LiftPower);
	FVector DragForce = WorldAirVelocity.GetSafeNormal() * NewtonsToKiloCentimeter(DragPower);
	FVector TorqueForce = this->GetRightVector() * (TorquePower * 10000.0f);

	//UE_LOG(LogTemp, Warning, TEXT("LiftPower: %f N"), LiftPower);
	//UE_LOG(LogTemp, Warning, TEXT("DragPower: %f N"), DragPower);
	//UE_LOG(LogTemp, Warning, TEXT("TorquePower: %f N"), TorquePower);

	//UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Speed: %f m/s, AoA: %f deg, Flap: %d"), Speed, AngleOfAttackDeg, FlapAngle);

	//UE_LOG(LogTemp, Warning, TEXT("LiftForce: %s kg⋅cm/s²"), *LiftForce.ToString());
	//UE_LOG(LogTemp, Warning, TEXT("DragForce: %s kg⋅cm/s²"), *DragForce.ToString());
	//UE_LOG(LogTemp, Warning, TEXT("TorqueForce: %s kg⋅cm/s²"), *TorqueForce.ToString());

	AerodynamicForce Result = AerodynamicForce(LiftForce, DragForce, TorqueForce, RelativePosition);
	FVector PositionalForce = Result.PositionalForce;
	FVector RotationalForce = Result.RotationalForce;
	//UE_LOG(LogTemp, Warning, TEXT("Surface Positional Force: %s"), *PositionalForce.ToString());
	//UE_LOG(LogTemp, Warning, TEXT("Surface Rotational Force: %s"), *RotationalForce.ToString());

	// Початкова точка: Центр тиску
	FVector StartLocation = CenterOfPressureInWorld;
	// Кінцева точка: Початкова точка + Вектор сили
	FVector EndLocation = StartLocation + PositionalForce;

	DrawDebugDirectionalArrow(
		GetWorld(),
		StartLocation,
		EndLocation,
		// Розмір наконечника: 25.0f
		25.0f,
		FColor::Green,
		false, // bPersistentLines: false
		-1.f,  // LifeTime: -1 (один кадр)
		0,     // DepthPriority: 0
		// Товщина стрілки: 5.0f. Можливо, варто використовувати меншу товщину, наприклад, 3.0f.
		5.0f);

	FVector StartLocationRotational = CenterOfPressureInWorld; // Трохи зміщуємо вгору
	// Кінцева точка: Початкова точка + Вектор сили
	FVector EndLocationRotational = StartLocationRotational + RotationalForce;

	DrawDebugDirectionalArrow(
		GetWorld(),
		StartLocationRotational,
		EndLocationRotational,
		25.0f,
		FColor::Blue, // Використовуємо інший колір (наприклад, червоний)
		false,
		-1.f,
		0,
		5.0f);

	/*DrawDebugDirectionalArrow(
		GetWorld(),
		CenterOfPressureInWorld,
		CenterOfPressureInWorld + (LiftForce / 100)
		,
		25.0f, FColor::Green, false, -1.f, 0, 5.0f);
	
	//World Air Velocity Direction show
	DrawDebugDirectionalArrow(
		GetWorld(),
		CenterOfPressureInWorld,
		CenterOfPressureInWorld + WorldAirVelocity * 20,
		25.0f, FColor::Green, false, -1.f, 0, 5.0f);

	//Linear Velocity Direction show
	DrawDebugDirectionalArrow(
		GetWorld(),
		CenterOfPressureInWorld,
		CenterOfPressureInWorld + LinearVelocity * 20,
		25.0f, FColor::Red, false, -1.f, 0, 5.0f);


	//Chord Direction show
	DrawDebugLine(
		GetWorld(),
		CenterOfPressureInWorld,
		CenterOfPressureInWorld + AverageChordDirection * 20.0f,
		FColor::Black, false, -1.f, 0, 5.0f
	);

	//Air Direction show
	DrawDebugDirectionalArrow(
		GetWorld(), 
		CenterOfPressureInWorld,
		CenterOfPressureInWorld + AirflowDirection * 20.0f,
		25.0f, FColor::Cyan, false, -1.f, 0, 5.0f);
	*/
	return Result;
}

float USubAerodynamicSurfaceSC::CalculateAngleOfAttackDeg(FVector WorldAirVelocity, FVector AverageChordDirection)
{
	FVector SurfaceUp = this->GetUpVector();
	float FlowAlongChord = FVector::DotProduct(WorldAirVelocity, AverageChordDirection);
	float FlowNormalToChord = FVector::DotProduct(WorldAirVelocity, SurfaceUp);
	float AngleOfAttackRad = FMath::Atan2(FlowNormalToChord, -FlowAlongChord);
	return FMath::RadiansToDegrees(AngleOfAttackRad);
}

float USubAerodynamicSurfaceSC::ToSpeedInMetersPerSecond(FVector WorldAirVelocity) {
	return WorldAirVelocity.Size() / 100.0f;
}

float USubAerodynamicSurfaceSC::CalculateLiftInNewtons(float AoA, int FlapAngle, float DynamicPressure) {
	FAerodynamicProfileRow* Profile = GetProfile(FlapAngle);
	float Cl = Profile->ClVsAoA.GetRichCurve()->Eval(AoA) / 100;
	UE_LOG(LogTemp, Warning, TEXT("Cl: %f"), Cl);
	return Cl * DynamicPressure * (SurfaceArea / 10000);
}

float USubAerodynamicSurfaceSC::CalculateDragInNewtons(float AoA, int FlapAngle, float DynamicPressure) {
	FAerodynamicProfileRow* Profile = GetProfile(FlapAngle);
	float Cd = Profile->CdVsAoA.GetRichCurve()->Eval(AoA) / 100;
	UE_LOG(LogTemp, Warning, TEXT("Cd: %f"), Cd);
	return Cd * DynamicPressure * (SurfaceArea / 10000);
}

float USubAerodynamicSurfaceSC::CalculateTorqueInNewtons(float AoA, int FlapAngle, float DynamicPressure, float ChordLength) {
	FAerodynamicProfileRow* Profile = GetProfile(FlapAngle);
	float Cm = Profile->CmVsAoA.GetRichCurve()->Eval(AoA) / 10000;
	UE_LOG(LogTemp, Warning, TEXT("Cm: %f"), Cm);
	return Cm * DynamicPressure* (SurfaceArea / 10000) * ChordLength;
}

FAerodynamicProfileRow* USubAerodynamicSurfaceSC::GetProfile(int FlapAngle)
{
	if (!AerodynamicTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("Aerodynamic table not set!"));
		return nullptr;
	}
	FName RowName = FName(*FString::Printf(TEXT("FLAP_%d_Deg"), FlapAngle));

	return AerodynamicTable->FindRow<FAerodynamicProfileRow>(RowName, TEXT("Looking up profile"));
}

FVector USubAerodynamicSurfaceSC::GetLiftDirection(FVector WorldAirVelocity) {
	FVector DragDirection = WorldAirVelocity.GetSafeNormal();
	FVector SpanDirection = this->GetRightVector();
	return FVector::CrossProduct(SpanDirection, DragDirection);
}


float USubAerodynamicSurfaceSC::NewtonsToKiloCentimeter(float Newtons) {
	return Newtons * 100;
}

float USubAerodynamicSurfaceSC::CalculateAvarageChordLength(Chord FirstChord, Chord SecondChord) {
	return ((FVector::Dist(FirstChord.StartPoint, FirstChord.EndPoint) / 100.0f) + (FVector::Dist(SecondChord.StartPoint, SecondChord.EndPoint) / 100.0f)) / 2.f;
}