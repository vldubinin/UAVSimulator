#include "FlightDynamicsComponent.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h" // Необхідно для GEngine
#include "Misc/OutputDevice.h"

// Конструктор компонента.
UFlightDynamicsComponent::UFlightDynamicsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_DuringPhysics;
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
		if (InitialSpeed > 0) {
			const FVector ForwardDirection = Owner->GetActorForwardVector();
			const FVector InitialVelocity = ForwardDirection * (InitialSpeed * 100.0f);
			Root->SetPhysicsLinearVelocity(InitialVelocity);
		}
	}

	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("######### СТАРТ СИМУЛЯЦІЇ #########\r\n"));
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Початкова позиція: %s"), *Root->GetComponentLocation().ToString());
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Гравітація увімкнена: %s"), Root->IsGravityEnabled() ? TEXT("Так") : TEXT("Ні"));
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Маса об'єкта: %.4f кг"), Root->GetMass());
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Симуляція фізики: %s"), Root->IsSimulatingPhysics() ? TEXT("Так") : TEXT("Ні"));
}

// Ця функція викликається кожного кадру гри.
void UFlightDynamicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickNumber++;

	if (!Owner || !Root || !WingCurveCl || !WingCurveCd || !TailCurveCl || !TailCurveCd) {
		if (GEngine) GEngine->AddOnScreenDebugMessage(0, 5.f, FColor::Red, TEXT("ERROR: FlightDynamicsComponent not configured!"));
		return;
	}
	
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("######### НОВИЙ ТІК %i #########"), TickNumber);
	LogMsg(TEXT("Швидкість: ") + FString::SanitizeFloat(GetSpeedInMetersPerSecond()) + TEXT("м/с"));
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Швидкість %.4f м/с."), GetSpeedInMetersPerSecond());
	
	// --- РОЗРАХУНОК СИЛИ ДВИГУНА ---
	ApplyEngineForce();

	// --- РОЗРАХУНОК АЕРОДИНАМІЧНИХ СИЛ ---
	ApplyTailForceForSide(+1);
	ApplyTailForceForSide(-1);
	ApplyWingForceForSide(+1);
	ApplyWingForceForSide(-1);

	if (bDrawDebugMarkers) DrawDebugCrosshairs(GetWorld(), Root->GetCenterOfMass(), FRotator::ZeroRotator, 15.0f * bDebugMarkersSize, FColor::Magenta, false, -1, 0);
}

void UFlightDynamicsComponent::ApplyEngineForce() {
	const FVector ForceDirection = Owner->GetActorForwardVector();
	const FVector EngineForce = ForceDirection * ThrustForce;
	const FVector WorldSpaceOffset = Owner->GetActorRotation().RotateVector(EngineForcePointOffset);
	const FVector EngineForcePoint = Root->GetCenterOfMass() + WorldSpaceOffset;
	Root->AddForceAtLocation(EngineForce, EngineForcePoint);

	const FVector ArrowEndPoint = EngineForcePoint + EngineForce;
	if (bDrawDebugMarkers) DrawDebugDirectionalArrow(GetWorld(), EngineForcePoint, ArrowEndPoint, 30.f, FColor::Red, false, 0.f, 0, 2.f * bDebugMarkersSize);
}

// Розрахунок сил для крила.
void UFlightDynamicsComponent::ApplyWingForceForSide(int32 Side)
{
	const FVector CenterOfMass = Root->GetCenterOfMass();
	const FQuat ComponentQuat = Root->GetComponentQuat();
	const FVector LocalOffset = FVector(WingForcePointOffset.X, Side * WingForcePointOffset.Y, WingForcePointOffset.Z);
	const FVector RotatedOffset = ComponentQuat.RotateVector(LocalOffset);
	const FVector ForcePoint = CenterOfMass + RotatedOffset;
	const FVector WingChordDirection = FindChordDirection(ForcePoint, WingLeadingEdgeOffset, WingTrailingEdgeOffset, Side);
	const FVector AirflowDirection = FindAirflowDirection(ForcePoint);

	const float AoA = CalculateAoA(AirflowDirection, WingChordDirection);

	// Застосовуємо силу опору
	const float DragForceInNewtons = CalculateDragInNewtons(WingCurveCd, WingArea, AoA);
	const float DragForceMagnitude = NewtonsToKiloCentimeter(DragForceInNewtons);
	const FVector DragForce = (AirflowDirection * DragForceMagnitude) * DragWingForseMultiplier;
	if (!DisableDragForse) Root->AddForceAtLocation(DragForce, ForcePoint);

	// Застосовуємо підйомну силу
	const FVector LiftDirection = FindLiftDirection(ForcePoint, AirflowDirection);
	const float LiftForceInNewtons = CalculateLiftInNewtons(WingCurveCl, WingArea, AoA);
	const float LiftForceMagnitude = NewtonsToKiloCentimeter(LiftForceInNewtons);
	const FVector LiftForce = (LiftDirection * LiftForceMagnitude) * LiftWingForseMultiplier;
	if (!DisableLiftForse) Root->AddForceAtLocation(LiftForce, ForcePoint);

	//Загальні логи для крила
	if (bDrawDebugMarkers && !DisableDragForse) DrawVectorAsArrow(GetWorld(), ForcePoint, DragForce, FColor::Purple, 0.01f * bDebugForceVectorSize, 4.0f, 1.0f);
	if (bDrawDebugMarkers && !DisableLiftForse) DrawVectorAsArrow(GetWorld(), ForcePoint, LiftForce, FColor::Green, 0.01f * bDebugForceVectorSize, 4.0f, 1.0f);
	if (bDrawDebugMarkers) DrawDebugCrosshairs(GetWorld(), ForcePoint, FRotator::ZeroRotator, 10.0f * bDebugMarkersSize, FColor::Red, false, -1, 0);
	if (bDrawDebugMarkers && !AirflowDirection.IsNearlyZero()) DrawDebugDirectionalArrow(GetWorld(), ForcePoint, ForcePoint + AirflowDirection * 300.f, 15.f, FColor::Blue, false, 0.f, 0, 0.2f * bDebugMarkersSize);

	if (DebugConsoleLogs && Side > 0) UE_LOG(LogTemp, Warning, TEXT("[Wing] AoA: %.4f -> Lf = %.4f N| Lift vector: %s | Df = %.4f N| Drag vector: %s"), AoA, LiftForceInNewtons, *LiftForce.ToString(), DragForceInNewtons, *DragForce.ToString());
}

// Розрахунок сил для хвоста.
void UFlightDynamicsComponent::ApplyTailForceForSide(int32 Side)
{
	const FVector CenterOfMass = Root->GetCenterOfMass();
	const FQuat ComponentQuat = Root->GetComponentQuat();
	const FVector LocalOffset = FVector(TailForcePointOffset.X, Side * TailForcePointOffset.Y, TailForcePointOffset.Z);
	const FVector RotatedOffset = ComponentQuat.RotateVector(LocalOffset);
	const FVector ForcePoint = CenterOfMass + RotatedOffset;
	const FVector TailChordDirection = FindChordDirection(ForcePoint, TailLeadingEdgeOffset, TailTrailingEdgeOffset, Side);
	const FVector AirflowDirection = FindAirflowDirection(ForcePoint);
	
	const float AoA = CalculateAoA(AirflowDirection, TailChordDirection);

	// Застосовуємо силу опору
	const float DragForceInNewtons = CalculateDragInNewtons(TailCurveCd, TailArea, AoA);
	const float DragForceMagnitude = NewtonsToKiloCentimeter(DragForceInNewtons);
	const FVector DragForce = (AirflowDirection * DragForceMagnitude) * DragTailForseMultiplier;
	if (!DisableDragForse) Root->AddForceAtLocation(DragForce, ForcePoint);

	// Застосовуємо підйомну силу
	const FVector LiftDirection = FindLiftDirection(ForcePoint, AirflowDirection) * -1.0f;
	const float LiftForceInNewtons = CalculateLiftInNewtons(TailCurveCl, TailArea, AoA);
	const float LiftForceMagnitude = NewtonsToKiloCentimeter(LiftForceInNewtons);
	const FVector LiftForce = (LiftDirection * LiftForceMagnitude) * LiftTailForseMultiplier;
	if (!DisableLiftForse) Root->AddForceAtLocation(LiftForce, ForcePoint);

	//Загальні логи для хвоста
	if (bDrawDebugMarkers && !DisableDragForse) DrawVectorAsArrow(GetWorld(), ForcePoint, DragForce, FColor::Purple, 0.01f * bDebugForceVectorSize, 4.0f, 1.0f);
	if (bDrawDebugMarkers && !DisableLiftForse) DrawVectorAsArrow(GetWorld(), ForcePoint, LiftForce, FColor::Green, 0.01f * bDebugForceVectorSize, 4.0f, 1.0f);
	if (bDrawDebugMarkers) DrawDebugCrosshairs(GetWorld(), ForcePoint, FRotator::ZeroRotator, 10.0f * bDebugMarkersSize, FColor::Red, false, -1, 0);
	if (bDrawDebugMarkers && !AirflowDirection.IsNearlyZero()) DrawDebugDirectionalArrow(GetWorld(), ForcePoint, ForcePoint + AirflowDirection * 300.f, 15.f, FColor::Blue, false, 0.f, 0, 0.2f * bDebugMarkersSize);

	if (DebugConsoleLogs && Side > 0) UE_LOG(LogTemp, Warning, TEXT("[Tail] AoA: %.4f -> Lf = %.4f N| Lift vector: %s | Df = %.4f N| Drag vector: %s"), AoA, LiftForceInNewtons, *LiftForce.ToString(), DragForceInNewtons, *DragForce.ToString());
}

float UFlightDynamicsComponent::NewtonsToKiloCentimeter(float Newtons) {
	return Newtons * 100;
}

float UFlightDynamicsComponent::CalculateLiftInNewtons(UCurveFloat* CurveCl, float Area, float AoA) {
	float Cl = CurveCl->GetFloatValue(AoA);
	const float AirSpeed = GetSpeedInMetersPerSecond();
	return Cl* ((AirDensity * (AirSpeed * AirSpeed)) / 2)* Area;
}

float UFlightDynamicsComponent::CalculateDragInNewtons(UCurveFloat* CurveCd, float Area, float AoA) {
	float Cd = CurveCd->GetFloatValue(AoA);
	const float Speed = GetSpeedInMetersPerSecond();
	return 0.5f * AirDensity * (Speed * Speed) * Cd * Area;
}

float UFlightDynamicsComponent::GetSpeedInMetersPerSecond() {
	return GetOwner()->GetVelocity().Size() / 100.0f;
}

FVector UFlightDynamicsComponent::FindChordDirection(FVector ForcePoint, FVector LeadingEdgeOffset, FVector TrailingEdgeOffset, int32 Side) {
	const FTransform& ActorTransform = Owner->GetActorTransform();

	// ForcePoint - це точка у світових координатах. Спочатку перетворимо її в локальні.
	const FVector LocalForcePoint = ActorTransform.InverseTransformPosition(ForcePoint);

	// Тепер додаємо локальні зміщення до локальної точки
	const FVector LocalLeadingEdge = LocalForcePoint + FVector(LeadingEdgeOffset.X, LeadingEdgeOffset.Y * Side, LeadingEdgeOffset.Z);
	const FVector LocalTrailingEdge = LocalForcePoint + FVector(TrailingEdgeOffset.X, TrailingEdgeOffset.Y * Side, TrailingEdgeOffset.Z);

	// І нарешті перетворюємо кінцеві локальні точки назад у світові координати
	const FVector WorldLeadingEdge = ActorTransform.TransformPosition(LocalLeadingEdge);
	const FVector WorldTrailingEdge = ActorTransform.TransformPosition(LocalTrailingEdge);

	if (bDrawDebugMarkers) DrawDebugLine(
		GetWorld(),
		WorldLeadingEdge,
		WorldTrailingEdge,
		FColor::Black,
		false, -1, 0, 0.5f * bDebugMarkersSize
	);

	return (WorldLeadingEdge - WorldTrailingEdge).GetSafeNormal();
}

FVector UFlightDynamicsComponent::FindAirflowDirection(FVector WorldOffset) {
	const FVector ActorVelocity = Owner->GetVelocity();

	if (ActorVelocity.IsNearlyZero())
	{
		return FVector();
	}
	return -ActorVelocity.GetSafeNormal();
}

FVector UFlightDynamicsComponent::FindLiftDirection(FVector WorldOffset, FVector AirflowDirection) {
	const FVector WingRightVector = Root->GetRightVector();
	FVector LiftDirection = FVector::CrossProduct(AirflowDirection, WingRightVector);
	LiftDirection.Normalize();
	if (LiftDirection.IsNearlyZero())
	{
		return FVector();
	}
	return LiftDirection;
	
}

float UFlightDynamicsComponent::CalculateAoA(FVector AirflowDirection, FVector ChordDirection) {
	const FVector Velocity = Owner->GetVelocity();
	if (!Velocity.IsNearlyZero())
	{
		const float DotProduct = FVector::DotProduct(ChordDirection, AirflowDirection);
		return FMath::RadiansToDegrees(FMath::Acos(DotProduct));
	}
	return 0.0f;
}

void UFlightDynamicsComponent::LogMsg(FString Text) {
	if (GEngine && DebugUILogs) {
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Black, Text);
	}
}

void UFlightDynamicsComponent::DrawVectorAsArrow(const UWorld* World, FVector StartLocation, FVector Direction, FColor Color, float Multiplier, float ArrowSize, float Thickness, float Duration)
{
	if (!World)
	{
		return;
	}
	FVector EndLocation = StartLocation + (Direction * Multiplier);

	DrawDebugDirectionalArrow(
		World,
		StartLocation,
		EndLocation,
		ArrowSize,
		Color,
		false,
		Duration,
		0,
		Thickness * bDebugMarkersSize
	);
}