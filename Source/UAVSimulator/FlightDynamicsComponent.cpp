#include "FlightDynamicsComponent.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h" // Необхідно для GEngine

// Конструктор компонента.
UFlightDynamicsComponent::UFlightDynamicsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// Ця функція викликається один раз на самому початку гри.
void UFlightDynamicsComponent::BeginPlay()
{
	Super::BeginPlay();

	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);
	Owner = GetOwner();
	if (Owner)
	{
		Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	}

	/*const float DesiredSpeed = 3612.0f;
	const FVector ForwardDirection = GetOwner()->GetActorForwardVector();
	const FVector InitialVelocity = ForwardDirection * DesiredSpeed;
	Root->SetPhysicsLinearVelocity(InitialVelocity);*/


	UE_LOG(LogTemp, Warning, TEXT("Початкова позиція: %s"), *Root->GetComponentLocation().ToString());
	UE_LOG(LogTemp, Warning, TEXT("Гравітація увімкнена: %s"), Root->IsGravityEnabled() ? TEXT("Так") : TEXT("Ні"));
	UE_LOG(LogTemp, Warning, TEXT("Маса об'єкта: %.4f кг"), Root->GetMass());
	UE_LOG(LogTemp, Warning, TEXT("Симуляція фізики: %s"), Root->IsSimulatingPhysics() ? TEXT("Так") : TEXT("Ні"));
}

// Ця функція викликається кожного кадру гри.
void UFlightDynamicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (WarmUpEngine && GetWorld()->GetTimeSeconds() < 0.2f)
	{
		return;
	}

	if (!Owner || !Root || !WingCurveCl || !WingCurveCd || !TailCurveCl || !TailCurveCd) {
		UE_LOG(LogTemp, Error, TEXT("🚫 Пропущено кадр: відсутній компонент або крива!"));
		if (GEngine) GEngine->AddOnScreenDebugMessage(0, 5.f, FColor::Red, TEXT("ERROR: FlightDynamicsComponent not configured!"));
		return;
	}

	const FVector CenterOfMass = Root->GetCenterOfMass();
	const FVector LinearVelocity = Root->GetPhysicsLinearVelocity();

	float SpeedInMetersPerSecond = GetSpeedInMetersPerSecond();
	LogMsg(TEXT("Швидкість: ") + FString::SanitizeFloat(SpeedInMetersPerSecond) + TEXT("м/с"));

	if (bDrawDebugMarkers)
	{
		DrawDebugCrosshairs(GetWorld(), CenterOfMass, FRotator::ZeroRotator, 15.0f, FColor::Magenta, false, -1, 0);
	}

	// --- РОЗРАХУНОК АЕРОДИНАМІЧНИХ СИЛ ---

	ApplyTailForceForSide(+1);
	LogMsg(TEXT("##### ПРАВИЙ ХВІСТ #####"));

	ApplyTailForceForSide(-1);
	LogMsg(TEXT("##### ЛІВИЙ ХВІСТ #####"));

	ApplyWingForceForSide(+1);
	LogMsg(TEXT("##### ПРАВЕ КРИЛО #####"));

	ApplyWingForceForSide(-1);
	LogMsg(TEXT("##### ЛІВЕ КРИЛО #####"));


	


	if (DebugIsLocalForce) {
		const FVector Forward = Root->GetForwardVector(); // Локальний +X у світі
		const FVector Right = Root->GetRightVector();
		const FVector Up = Root->GetUpVector();
		const FVector Force = (Forward * DebugForce.X) + (Right * DebugForce.Y) + (Up * DebugForce.Z);
		Root->AddForce(Force);
	}
	else {
		Root->AddForce(DebugForce);
	}

}

// Розрахунок сил для крила.
void UFlightDynamicsComponent::ApplyWingForceForSide(int32 Side)
{
	const FVector CenterOfMass = Root->GetCenterOfMass();
	const FQuat ComponentQuat = Root->GetComponentQuat();
	const FVector LocalOffset = FVector(WingForcePointOffset.X, Side * WingForcePointOffset.Y, WingForcePointOffset.Z);
	const FVector RotatedOffset = ComponentQuat.RotateVector(LocalOffset);
	const FVector ForcePoint = CenterOfMass + RotatedOffset;

	if (bDrawDebugMarkers)
	{
		DrawDebugCrosshairs(GetWorld(), ForcePoint, FRotator::ZeroRotator, 10.0f, FColor::Red, false, -1, 0);
	}
	const FRotator WingChordConfiguration = GetWingChordConfiguration(Side);
	const FVector WingChordDirection = FindChordDirection(ForcePoint, WingChordConfiguration, DebugDrawWingsInfo);
	const FVector AirflowDirection = FindAirflowDirection(ForcePoint, DebugDrawWingsInfo);
	const FVector LiftDirection = FindLiftDirection(ForcePoint, AirflowDirection, DebugDrawWingsInfo);
	//const float AoA = CalculateAoA(AirflowDirection, WingChordDirection);
	const float AoA = CalculateAoASimple(AirflowDirection, WingChordDirection);

	const float DragForceInNewtons = CalculateDragInNewtons(WingCurveCd, WingArea, AoA);
	const float LiftForceInNewtons = CalculateLiftInNewtons(WingCurveCl, WingArea, AoA);
	LogMsg(TEXT("Cd: ") + FString::SanitizeFloat(DragForceInNewtons) + TEXT(" Н, Cl: ") + FString::SanitizeFloat(LiftForceInNewtons) + TEXT(" Н."));

	// --- Застосування обох сил ---
	if (!DebugDisableWings && !DebugDisableDragForceWings) {
		// Застосовуємо силу опору
		const float DragForceMagnitude = NewtonsToKiloCentimeter(DragForceInNewtons);
		const FVector DragForce = AirflowDirection * DragForceMagnitude;
		Root->AddForceAtLocation(DragForce, ForcePoint);
	}
	if (!DebugDisableWings && !DebugDisableLiftForceWings) {
		// Застосовуємо підйомну силу
		const float LiftForceMagnitude = NewtonsToKiloCentimeter(LiftForceInNewtons);
		const FVector LiftForce = LiftDirection * LiftForceMagnitude;
		Root->AddForceAtLocation(LiftForce, ForcePoint);
	}
}

// Розрахунок сил для хвоста.
void UFlightDynamicsComponent::ApplyTailForceForSide(int32 Side)
{
	const FVector CenterOfMass = Root->GetCenterOfMass();
	const FQuat ComponentQuat = Root->GetComponentQuat();
	const FVector LocalOffset = FVector(TailForcePointOffset.X, Side * TailForcePointOffset.Y, TailForcePointOffset.Z);
	const FVector RotatedOffset = ComponentQuat.RotateVector(LocalOffset);
	const FVector ForcePoint = CenterOfMass + RotatedOffset;

	if (bDrawDebugMarkers)
	{
		DrawDebugCrosshairs(GetWorld(), ForcePoint, FRotator::ZeroRotator, 10.0f, FColor::Red, false, -1, 0);
	}
	const FRotator TailChordConfiguration = GetTailChordConfiguration(Side);
	const FVector TailChordDirection = FindChordDirection(ForcePoint, TailChordConfiguration, DebugDrawTailInfo);
	const FVector AirflowDirection = FindAirflowDirection(ForcePoint, DebugDrawTailInfo);
	const FVector LiftDirection = FindLiftDirection(ForcePoint, AirflowDirection, DebugDrawTailInfo);
	//const float AoA = CalculateAoA(AirflowDirection, TailChordDirection);
	const float AoA = CalculateAoASimple(AirflowDirection, TailChordDirection);

	const float DragForceInNewtons = CalculateDragInNewtons(TailCurveCd, TailArea, AoA);
	const float LiftForceInNewtons = CalculateLiftInNewtons(TailCurveCl, TailArea, AoA);
	LogMsg(TEXT("Cd: ") + FString::SanitizeFloat(DragForceInNewtons) + TEXT(" Н, Cl: ") + FString::SanitizeFloat(LiftForceInNewtons) + TEXT(" Н."));


	// --- Застосування обох сил ---
	if (!DebugDisableTail && !DebugDisableDragForceTail) {
		// Застосовуємо силу опору
		const float DragForceMagnitude = NewtonsToKiloCentimeter(DragForceInNewtons);
		const FVector DragForce = AirflowDirection * DragForceMagnitude;
		Root->AddForceAtLocation(DragForce, ForcePoint);
	}
	if (!DebugDisableTail && !DebugDisableLiftForceTail) {
		// Застосовуємо підйомну силу
		const float LiftForceMagnitude = NewtonsToKiloCentimeter(LiftForceInNewtons);
		const FVector LiftForce = LiftDirection * LiftForceMagnitude;
		Root->AddForceAtLocation(LiftForce, ForcePoint);
	}
}

float UFlightDynamicsComponent::NewtonsToKiloCentimeter(float Newtons) {
	return Newtons * 100;
}

float UFlightDynamicsComponent::CalculateLiftInNewtons(UCurveFloat* CurveCl, float Area, float AoA) {
	float Cl = CurveCl->GetFloatValue(AoA);
	const float AirSpeed = GetSpeedInMetersPerSecond();

	float result = Cl* ((AirDensity * (AirSpeed * AirSpeed)) / 2)* Area;
	UE_LOG(LogTemp, Warning, TEXT("Lf = %.4f Н| ρ: %.4f м/с., v: %.4f, A: %.4f, Cl: %.4f [AoA: %.4f]"), result, AirDensity, AirSpeed, Area, Cl, AoA);
	return result;
}

float UFlightDynamicsComponent::CalculateDragInNewtons(UCurveFloat* CurveCd, float Area, float AoA) {
	float Cd = CurveCd->GetFloatValue(AoA);
	const float Speed = GetSpeedInMetersPerSecond();
	float result = 0.5f * AirDensity * (Speed * Speed) * Cd * Area;
	UE_LOG(LogTemp, Warning, TEXT("Df = %.4f Н| ρ: %.4f м/с., v: %.4f, A: %.4f, Cd : %.4f [AoA: %.4f]"), result, AirDensity, Speed, Area, Cd, AoA);
	return result;
}

float UFlightDynamicsComponent::GetSpeedInMetersPerSecond() {
	return GetOwner()->GetVelocity().Size() / 100.0f;
}

FVector UFlightDynamicsComponent::FindChordDirection(FVector WorldOffset, FRotator TiltRotator, bool Draw) {
	const float LineLength = 20.0f;
	const FColor Color = FColor::Red;
	const FVector LocalTiltedDirection = TiltRotator.RotateVector(FVector::ForwardVector);
	const FVector Direction = Owner->GetActorTransform().GetRotation().RotateVector(LocalTiltedDirection).GetSafeNormal();;
	
	if (Draw) {
		const FVector Start = WorldOffset - Direction * (LineLength / 2.0f);
		const FVector End = WorldOffset + Direction * (LineLength / 2.0f);
		DrawDebugLine(GetWorld(), Start, End, Color, false, -1.0f, 0, 1.0f);
	}
	return Direction;
}

FRotator UFlightDynamicsComponent::GetWingChordConfiguration(int32 Side) {
	return FRotator(WingChordYAngle, WingChordZAngle * Side, 0.0f);
}

FRotator UFlightDynamicsComponent::GetTailChordConfiguration(int32 Side) {
	return FRotator(TailChordYAngle, TailChordZAngle * Side, 0.0f);
}

FVector UFlightDynamicsComponent::FindAirflowDirection(FVector WorldOffset, bool Draw) {
	const FVector ActorVelocity = Owner->GetVelocity();

	if (!ActorVelocity.IsNearlyZero())
	{
		const FVector AirflowDirection = -ActorVelocity.GetSafeNormal();

		if (Draw) {
			const float StartDistance = 15.0f;
			const float LineLength = 50.0f;

			const FVector StartPoint = WorldOffset;
			const FVector EndPoint = StartPoint + AirflowDirection * LineLength;

			DrawDebugLine(GetWorld(), StartPoint, EndPoint, FColor::Cyan, false, -1.0f, 0, 1.0f);
		}

		return AirflowDirection;
	}
	return FVector();
}

FVector UFlightDynamicsComponent::FindLiftDirection(FVector WorldOffset, FVector AirflowDirection, bool Draw) {
	const FVector WingRightVector = Root->GetRightVector();
	FVector LiftDirection = FVector::CrossProduct(AirflowDirection, WingRightVector);
	LiftDirection.Normalize();
	if (!LiftDirection.IsNearlyZero())
	{
		if (Draw) {
			const FVector StartPoint = WorldOffset;
			const FVector EndPoint = StartPoint + LiftDirection * 50;

			DrawDebugLine(GetWorld(), StartPoint, EndPoint, FColor::Blue, false, -1.0f, 0, 1.0f);
		}
		return LiftDirection;
	}
	return FVector();
}

float UFlightDynamicsComponent::CalculateAoASimple(FVector AirflowDirection, FVector WingChordDirection) {
	FVector ActorForwardVector = GetOwner()->GetActorForwardVector();

	// 2. Розрахуйте скалярний добуток (Dot Product)
	// Обидва вектори мають бути нормалізованими для коректного результату
	float DotProduct = FVector::DotProduct(ActorForwardVector, -AirflowDirection);

	// 3. Розрахуйте кут в градусах за допомогою арккосинуса
	// FMath::Acos дає результат в радіанах, тому конвертуємо в градуси
	float AngleOfAttackRadians = FMath::Acos(DotProduct);
	float AoA = FMath::RadiansToDegrees(AngleOfAttackRadians);

	UE_LOG(LogTemp, Warning, TEXT("Airflow: %s, Up: %s, AoA: %.4f"), *AirflowDirection.ToString(), *Root->GetUpVector().ToString(), AoA);
	return AoA;
}

float UFlightDynamicsComponent::CalculateAoA(FVector AirflowDirection, FVector WingChordDirection) {
	const float AngleDotProduct = FVector::DotProduct(-AirflowDirection, WingChordDirection);
	const float AngleRadians = FMath::Acos(FMath::Clamp(AngleDotProduct, -1.0f, 1.0f));
	float AoA = FMath::RadiansToDegrees(AngleRadians);

	const float SignDotProduct = FVector::DotProduct(AirflowDirection, Root->GetUpVector());

	AoA *= -FMath::Sign(SignDotProduct);

	UE_LOG(LogTemp, Warning, TEXT("Airflow: %s, Chord: %s, Up: %s, AoA: %.4f"), *AirflowDirection.ToString(), *WingChordDirection.ToString(), *Root->GetUpVector().ToString(), AoA);
	return AoA;
}

void UFlightDynamicsComponent::LogMsg(FString Text) {
	if (GEngine && bDrawDebugMarkers) {
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Black, Text);
	}
}
