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


	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Початкова позиція: %s"), *Root->GetComponentLocation().ToString());
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Гравітація увімкнена: %s"), Root->IsGravityEnabled() ? TEXT("Так") : TEXT("Ні"));
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Маса об'єкта: %.4f кг"), Root->GetMass());
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Симуляція фізики: %s"), Root->IsSimulatingPhysics() ? TEXT("Так") : TEXT("Ні"));
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
		if (DebugConsoleLogs) UE_LOG(LogTemp, Error, TEXT("🚫 Пропущено кадр: відсутній компонент або крива!"));
		if (GEngine) GEngine->AddOnScreenDebugMessage(0, 5.f, FColor::Red, TEXT("ERROR: FlightDynamicsComponent not configured!"));
		return;
	}

	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("######### НОВИЙ ТІК #########\r\n"));

	const FVector CenterOfMass = Root->GetCenterOfMass();
	const FVector LinearVelocity = Root->GetPhysicsLinearVelocity();

	float SpeedInMetersPerSecond = GetSpeedInMetersPerSecond();
	LogMsg(TEXT("Швидкість: ") + FString::SanitizeFloat(SpeedInMetersPerSecond) + TEXT("м/с"));
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Швидкість %.4f м/с."), SpeedInMetersPerSecond);

	if (bDrawDebugMarkers)
	{
		DrawDebugCrosshairs(GetWorld(), CenterOfMass, FRotator::ZeroRotator, 15.0f, FColor::Magenta, false, -1, 0);
	}

	// --- РОЗРАХУНОК СИЛИ ДВИГУНА ---
		// 1. Визначаємо напрямок сили. Зазвичай це напрямок "вперед" для актора.
	const FVector ForceDirection = Owner->GetActorForwardVector();

	// 2. Розраховуємо вектор сили, множачи напрямок на величину.
	const FVector EngineForce = ForceDirection * ThrustForce;

	// 3. Розраховуємо точку прикладання сили в світових координатах.
	// Для цього потрібно повернути локальне зміщення відповідно до поточної орієнтації актора
	// і додати його до світової позиції центру мас.
	const FVector WorldSpaceOffset = Owner->GetActorRotation().RotateVector(EngineForcePointOffset);
	const FVector EngineForcePoint = Root->GetCenterOfMass() + WorldSpaceOffset;

	// 4. Прикладаємо силу до розрахованої точки. ЦЕ КЛЮЧОВИЙ МОМЕНТ.
	// AddForceAtLocation створить як лінійний, так і обертовий рух.
	Root->AddForceAtLocation(EngineForce, EngineForcePoint);

	// 5. Правильно візуалізуємо вектор сили для відлагодження.
	// Малюємо стрілку від точки прикладання сили в напрямку сили.
	const FVector ArrowEndPoint = EngineForcePoint + EngineForce;
	DrawDebugDirectionalArrow(GetWorld(), EngineForcePoint, ArrowEndPoint, 30.f, FColor::Red, false, 0.f, 0, 2.f);

	// --- РОЗРАХУНОК АЕРОДИНАМІЧНИХ СИЛ ---

	ApplyTailForceForSide(+1);
	LogMsg(TEXT("##### ПРАВИЙ ХВІСТ #####"));

	ApplyTailForceForSide(-1);
	LogMsg(TEXT("##### ЛІВИЙ ХВІСТ #####"));

	ApplyWingForceForSide(+1);
	LogMsg(TEXT("##### ПРАВЕ КРИЛО #####"));

	ApplyWingForceForSide(-1);
	LogMsg(TEXT("##### ЛІВЕ КРИЛО #####"));
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

	if (bDrawDebugMarkers)
	{
		DrawDebugCrosshairs(GetWorld(), ForcePoint, FRotator::ZeroRotator, 10.0f, FColor::Red, false, -1, 0);
	}


	if (bDrawDebugMarkers && !AirflowDirection.IsNearlyZero())
	{
		DrawDebugDirectionalArrow(GetWorld(), ForcePoint, ForcePoint + AirflowDirection * 300.f, 15.f, FColor::Blue, false, 0.f, 0, 0.2f);
	}

	const float AoA = CalculateAoA(AirflowDirection, WingChordDirection);

	const float DragForceInNewtons = CalculateDragInNewtons(WingCurveCd, WingArea, AoA);
	const float LiftForceInNewtons = CalculateLiftInNewtons(WingCurveCl, WingArea, AoA);
	LogMsg(TEXT("Cd: ") + FString::SanitizeFloat(DragForceInNewtons) + TEXT(" Н, Cl: ") + FString::SanitizeFloat(LiftForceInNewtons) + TEXT(" Н."));

	// Застосовуємо силу опору
	const float DragForceMagnitude = NewtonsToKiloCentimeter(DragForceInNewtons);
	const FVector DragForce = (AirflowDirection * DragForceMagnitude) * DragWingForseMultiplier;
	if (!DisableDragForse) Root->AddForceAtLocation(DragForce, ForcePoint);
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Wing drag: %s"), *DragForce.ToString());
	if (bDrawDebugMarkers && !DisableDragForse) DrawVectorAsArrow(GetWorld(), ForcePoint, DragForce, FColor::Purple, 10.0f);

	// Застосовуємо підйомну силу
	const FVector LiftDirection = FindLiftDirection(ForcePoint, AirflowDirection);
	const float LiftForceMagnitude = NewtonsToKiloCentimeter(LiftForceInNewtons);
	const FVector LiftForce = (LiftDirection * LiftForceMagnitude) * LiftWingForseMultiplier;

	if (!DisableLiftForse) Root->AddForceAtLocation(LiftForce, ForcePoint);

	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Wing lift: %s"), *LiftForce.ToString());
	if (bDrawDebugMarkers && !DisableLiftForse) DrawVectorAsArrow(GetWorld(), ForcePoint, LiftForce, FColor::Green, 10.0f);
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

	if (bDrawDebugMarkers)
	{
		DrawDebugCrosshairs(GetWorld(), ForcePoint, FRotator::ZeroRotator, 10.0f, FColor::Red, false, -1, 0);
	}

	if (bDrawDebugMarkers && !AirflowDirection.IsNearlyZero())
	{
		DrawDebugDirectionalArrow(GetWorld(), ForcePoint, ForcePoint + AirflowDirection * 300.f, 15.f, FColor::Blue, false, 0.f, 0, 0.2f);
	}
	
	const float AoA = CalculateAoA(AirflowDirection, TailChordDirection);

	const float DragForceInNewtons = CalculateDragInNewtons(TailCurveCd, TailArea, AoA);
	const float LiftForceInNewtons = CalculateLiftInNewtons(TailCurveCl, TailArea, AoA);
	LogMsg(TEXT("Cd: ") + FString::SanitizeFloat(DragForceInNewtons) + TEXT(" Н, Cl: ") + FString::SanitizeFloat(LiftForceInNewtons) + TEXT(" Н."));

	// Застосовуємо силу опору
	const float DragForceMagnitude = NewtonsToKiloCentimeter(DragForceInNewtons);
	const FVector DragForce = (AirflowDirection * DragForceMagnitude) * DragTailForseMultiplier;
	if (!DisableDragForse) Root->AddForceAtLocation(DragForce, ForcePoint);
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Tail drag: %s"), *DragForce.ToString());
	if (bDrawDebugMarkers && !DisableDragForse) DrawVectorAsArrow(GetWorld(), ForcePoint, DragForce, FColor::Purple, 10.0f);

	// Застосовуємо підйомну силу
	const FVector LiftDirection = FindLiftDirection(ForcePoint, AirflowDirection);
	const float LiftForceMagnitude = NewtonsToKiloCentimeter(LiftForceInNewtons);
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Tail LiftForceMagnitude: %.4f"), LiftForceMagnitude);

	const FVector LiftForce = (LiftDirection * LiftForceMagnitude) * LiftTailForseMultiplier;
	if (!DisableLiftForse) Root->AddForceAtLocation(LiftForce, ForcePoint);
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Tail lift: %s"), *LiftForce.ToString());
	if (bDrawDebugMarkers && !DisableLiftForse) DrawVectorAsArrow(GetWorld(), ForcePoint, LiftForce, FColor::Green, 10.0f);
}

float UFlightDynamicsComponent::NewtonsToKiloCentimeter(float Newtons) {
	return Newtons * 100;
}

float UFlightDynamicsComponent::CalculateLiftInNewtons(UCurveFloat* CurveCl, float Area, float AoA) {
	float Cl = CurveCl->GetFloatValue(AoA);
	const float AirSpeed = GetSpeedInMetersPerSecond();

	float result = Cl* ((AirDensity * (AirSpeed * AirSpeed)) / 2)* Area;
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Lf = %.4f Н| ρ: %.4f м/с., v: %.4f, A: %.4f, Cl: %.4f [AoA: %.4f]"), result, AirDensity, AirSpeed, Area, Cl, AoA);
	return result;
}

float UFlightDynamicsComponent::CalculateDragInNewtons(UCurveFloat* CurveCd, float Area, float AoA) {
	float Cd = CurveCd->GetFloatValue(AoA);
	const float Speed = GetSpeedInMetersPerSecond();
	float result = 0.5f * AirDensity * (Speed * Speed) * Cd * Area;
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("Df = %.4f Н| ρ: %.4f м/с., v: %.4f, A: %.4f, Cd : %.4f [AoA: %.4f]"), result, AirDensity, Speed, Area, Cd, AoA);
	return result;
}

float UFlightDynamicsComponent::GetSpeedInMetersPerSecond() {
	return GetOwner()->GetVelocity().Size() / 100.0f;
}

FVector UFlightDynamicsComponent::FindChordDirection(FVector ForcePoint, FVector LeadingEdgeOffset, FVector TrailingEdgeOffset, int32 Side) {
	/*const FTransform& ActorTransform = Owner->GetActorTransform();
	const FVector LeadingEdgeLocation = ActorTransform.TransformPosition(FVector(LeadingEdgeOffset.X + ForcePoint.X, (LeadingEdgeOffset.Y * Side) + ForcePoint.Y, LeadingEdgeOffset.Z + ForcePoint.Z));
	const FVector TrailingEdgeLocation = ActorTransform.TransformPosition(FVector(TrailingEdgeOffset.X + ForcePoint.X, (TrailingEdgeOffset.Y * Side) + ForcePoint.Y, TrailingEdgeOffset.Z + ForcePoint.Z));

	// Візуалізація хорди
	if (DebugConsoleLogs) DrawDebugLine(
		GetWorld(),
		LeadingEdgeLocation,
		TrailingEdgeLocation,
		FColor::Black,
		false, -1, 0, 0.5f
	);
	return (LeadingEdgeLocation - TrailingEdgeLocation).GetSafeNormal();*/
	const FTransform& ActorTransform = Owner->GetActorTransform();

	// 1. Спершу перетворюємо базові локальні зміщення у світові координати
	const FVector BaseLeadingEdgeWorld = ActorTransform.TransformPosition(
		FVector(LeadingEdgeOffset.X, LeadingEdgeOffset.Y * Side, LeadingEdgeOffset.Z)
	);
	const FVector BaseTrailingEdgeWorld = ActorTransform.TransformPosition(
		FVector(TrailingEdgeOffset.X, TrailingEdgeOffset.Y * Side, TrailingEdgeOffset.Z)
	);

	// 2. Тепер, коли ми у світовому просторі, ми можемо додати ForcePoint.
	//    Це спрацює правильно, ЯКЩО ForcePoint - це теж вектор у світовому просторі.
	const FVector LeadingEdgeLocation = BaseLeadingEdgeWorld + ForcePoint;
	const FVector TrailingEdgeLocation = BaseTrailingEdgeWorld + ForcePoint;

	// Візуалізація хорди
	if (DebugConsoleLogs) DrawDebugLine(
		GetWorld(),
		LeadingEdgeLocation,
		TrailingEdgeLocation,
		FColor::Black,
		false, -1, 0, 0.5f
	);
	return (LeadingEdgeLocation - TrailingEdgeLocation).GetSafeNormal();
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

float UFlightDynamicsComponent::CalculateAoA(FVector AirflowDirection, FVector WingChordDirection) {
	const FVector Velocity = Owner->GetVelocity();
	if (DebugConsoleLogs) UE_LOG(LogTemp, Warning, TEXT("AirflowDirection: %s, WingChordDirection: %s"), *AirflowDirection.ToString(), *WingChordDirection.ToString());

	if (!Velocity.IsNearlyZero())
	{
		const float DotProduct = FVector::DotProduct(WingChordDirection, AirflowDirection);
		return FMath::RadiansToDegrees(FMath::Acos(DotProduct));
	}
	return 0.0f;
}

void UFlightDynamicsComponent::LogMsg(FString Text) {
	if (GEngine && DebugUILogs) {
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Black, Text);
	}
}

void UFlightDynamicsComponent::DrawVectorAsArrow(const UWorld* World, FVector StartLocation, FVector Direction, FColor Color, float Multiplier, float ArrowSize, float Duration, float Thickness)
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
		Thickness
	);
}