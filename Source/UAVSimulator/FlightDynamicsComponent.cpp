#include "FlightDynamicsComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/AerodynamicPhysicsLibrary.h"
#include "UAVSimulator/Util/AerodynamicDebugRenderer.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

UFlightDynamicsComponent::UFlightDynamicsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_DuringPhysics;
}

void UFlightDynamicsComponent::BeginPlay()
{
	Super::BeginPlay();

	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);
	Owner = GetOwner();
	if (Owner)
	{
		Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
		if (InitialSpeed > 0.f)
		{
			const FVector InitialVelocity = Owner->GetActorForwardVector() * (InitialSpeed * 100.0f);
			Root->SetPhysicsLinearVelocity(InitialVelocity);
		}
	}

	if (DebugConsoleLogs) UE_LOG(LogUAV, Warning, TEXT("######### СТАРТ СИМУЛЯЦІЇ #########\r\n"));
	if (DebugConsoleLogs) UE_LOG(LogUAV, Warning, TEXT("Початкова позиція: %s"), *Root->GetComponentLocation().ToString());
	if (DebugConsoleLogs) UE_LOG(LogUAV, Warning, TEXT("Гравітація увімкнена: %s"), Root->IsGravityEnabled() ? TEXT("Так") : TEXT("Ні"));
	if (DebugConsoleLogs) UE_LOG(LogUAV, Warning, TEXT("Маса об'єкта: %.4f кг"), Root->GetMass());
	if (DebugConsoleLogs) UE_LOG(LogUAV, Warning, TEXT("Симуляція фізики: %s"), Root->IsSimulatingPhysics() ? TEXT("Так") : TEXT("Ні"));
}

void UFlightDynamicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickNumber++;

	if (!Owner || !Root || !WingCurveCl || !WingCurveCd || !TailCurveCl || !TailCurveCd)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(0, 5.f, FColor::Red, TEXT("ERROR: FlightDynamicsComponent not configured!"));
		return;
	}

	if (DebugConsoleLogs) UE_LOG(LogUAV, Warning, TEXT("######### НОВИЙ ТІК %i #########"), TickNumber);
	LogMsg(TEXT("Швидкість: ") + FString::SanitizeFloat(GetSpeedInMetersPerSecond()) + TEXT("м/с"));
	if (DebugConsoleLogs) UE_LOG(LogUAV, Warning, TEXT("Швидкість %.4f м/с."), GetSpeedInMetersPerSecond());

	ApplyEngineForce();
	ApplyTailForceForSide(+1);
	ApplyTailForceForSide(-1);
	ApplyWingForceForSide(+1);
	ApplyWingForceForSide(-1);

	if (bDrawDebugMarkers)
	{
		DrawDebugCrosshairs(GetWorld(), Root->GetCenterOfMass(), FRotator::ZeroRotator, 15.0f * bDebugMarkersSize, FColor::Magenta, false, -1, 0);
	}
}

void UFlightDynamicsComponent::ApplyEngineForce()
{
	const FVector ForceDirection = Owner->GetActorForwardVector();
	const FVector EngineForce = ForceDirection * ThrustForce;
	const FVector WorldSpaceOffset = Owner->GetActorRotation().RotateVector(EngineForcePointOffset);
	const FVector EngineForcePoint = Root->GetCenterOfMass() + WorldSpaceOffset;
	Root->AddForceAtLocation(EngineForce, EngineForcePoint);

	if (bDrawDebugMarkers)
	{
		const FVector ArrowEnd = EngineForcePoint + EngineForce;
		DrawDebugDirectionalArrow(GetWorld(), EngineForcePoint, ArrowEnd, 30.f, FColor::Red, false, 0.f, 0, 2.f * bDebugMarkersSize);
	}
}

void UFlightDynamicsComponent::ApplyWingForceForSide(int32 Side)
{
	FSurfaceForceParams P;
	P.ForcePointOffset   = WingForcePointOffset;
	P.LeadingEdgeOffset  = WingLeadingEdgeOffset;
	P.TrailingEdgeOffset = WingTrailingEdgeOffset;
	P.Area               = WingArea;
	P.CurveCl            = WingCurveCl;
	P.CurveCd            = WingCurveCd;
	P.LiftMultiplier     = LiftWingForseMultiplier;
	P.DragMultiplier     = DragWingForseMultiplier;
	P.bInvertLift        = false;
	ApplyAerodynamicForceForSide(Side, P, DebugConsoleLogs && Side > 0);
}

void UFlightDynamicsComponent::ApplyTailForceForSide(int32 Side)
{
	FSurfaceForceParams P;
	P.ForcePointOffset   = TailForcePointOffset;
	P.LeadingEdgeOffset  = TailLeadingEdgeOffset;
	P.TrailingEdgeOffset = TailTrailingEdgeOffset;
	P.Area               = TailArea;
	P.CurveCl            = TailCurveCl;
	P.CurveCd            = TailCurveCd;
	P.LiftMultiplier     = LiftTailForseMultiplier;
	P.DragMultiplier     = DragTailForseMultiplier;
	P.bInvertLift        = true;
	ApplyAerodynamicForceForSide(Side, P, DebugConsoleLogs && Side > 0);
}

void UFlightDynamicsComponent::ApplyAerodynamicForceForSide(int32 Side, const FSurfaceForceParams& Params, bool bLog)
{
	const FVector CenterOfMass   = Root->GetCenterOfMass();
	const FQuat   ComponentQuat  = Root->GetComponentQuat();
	const FVector LocalOffset    = FVector(Params.ForcePointOffset.X, Side * Params.ForcePointOffset.Y, Params.ForcePointOffset.Z);
	const FVector ForcePoint     = CenterOfMass + ComponentQuat.RotateVector(LocalOffset);
	const FVector ChordDirection = FindChordDirection(ForcePoint, Params.LeadingEdgeOffset, Params.TrailingEdgeOffset, Side);
	const FVector AirflowDir     = FindAirflowDirection(ForcePoint);
	const float   AoA            = CalculateAoA(AirflowDir, ChordDirection);

	// Drag
	const float DragNewtons   = CalculateDragInNewtons(Params.CurveCd, Params.Area, AoA);
	const float DragMagnitude = UAerodynamicPhysicsLibrary::NewtonsToKiloCentimeter(DragNewtons);
	const FVector DragForce   = (AirflowDir * DragMagnitude) * Params.DragMultiplier;
	if (!DisableDragForse) Root->AddForceAtLocation(DragForce, ForcePoint);

	// Lift
	FVector LiftDirection = FindLiftDirection(ForcePoint, AirflowDir);
	if (Params.bInvertLift) LiftDirection *= -1.f;
	const float LiftNewtons   = CalculateLiftInNewtons(Params.CurveCl, Params.Area, AoA);
	const float LiftMagnitude = UAerodynamicPhysicsLibrary::NewtonsToKiloCentimeter(LiftNewtons);
	const FVector LiftForce   = (LiftDirection * LiftMagnitude) * Params.LiftMultiplier;
	if (!DisableLiftForse) Root->AddForceAtLocation(LiftForce, ForcePoint);

	// Debug visualization
	if (bDrawDebugMarkers && !DisableDragForse)
		AerodynamicDebugRenderer::DrawForceArrow(GetWorld(), ForcePoint, DragForce, FColor::Purple, 0.01f * bDebugForceVectorSize, 4.0f, 1.0f, -1.f);
	if (bDrawDebugMarkers && !DisableLiftForse)
		AerodynamicDebugRenderer::DrawForceArrow(GetWorld(), ForcePoint, LiftForce, FColor::Green,  0.01f * bDebugForceVectorSize, 4.0f, 1.0f, -1.f);
	if (bDrawDebugMarkers)
		DrawDebugCrosshairs(GetWorld(), ForcePoint, FRotator::ZeroRotator, 10.0f * bDebugMarkersSize, FColor::Red, false, -1, 0);
	if (bDrawDebugMarkers && !AirflowDir.IsNearlyZero())
		DrawDebugDirectionalArrow(GetWorld(), ForcePoint, ForcePoint + AirflowDir * 300.f, 15.f, FColor::Blue, false, 0.f, 0, 0.2f * bDebugMarkersSize);

	if (bLog)
		UE_LOG(LogUAV, Warning, TEXT("[Surface] AoA: %.4f -> Lf = %.4f N | Lv: %s | Df = %.4f N | Dv: %s"),
			AoA, LiftNewtons, *LiftForce.ToString(), DragNewtons, *DragForce.ToString());
}

float UFlightDynamicsComponent::CalculateLiftInNewtons(UCurveFloat* CurveCl, float Area, float AoA)
{
	float Cl = CurveCl->GetFloatValue(AoA);
	const float Speed = GetSpeedInMetersPerSecond();
	return Cl * ((AirDensity * (Speed * Speed)) / 2.f) * Area;
}

float UFlightDynamicsComponent::CalculateDragInNewtons(UCurveFloat* CurveCd, float Area, float AoA)
{
	float Cd = CurveCd->GetFloatValue(AoA);
	const float Speed = GetSpeedInMetersPerSecond();
	return 0.5f * AirDensity * (Speed * Speed) * Cd * Area;
}

float UFlightDynamicsComponent::GetSpeedInMetersPerSecond()
{
	return GetOwner()->GetVelocity().Size() / 100.0f;
}

FVector UFlightDynamicsComponent::FindChordDirection(FVector ForcePoint, FVector LeadingEdgeOffset, FVector TrailingEdgeOffset, int32 Side)
{
	const FTransform& ActorTransform = Owner->GetActorTransform();
	const FVector LocalForcePoint = ActorTransform.InverseTransformPosition(ForcePoint);

	const FVector LocalLeading  = LocalForcePoint + FVector(LeadingEdgeOffset.X,  LeadingEdgeOffset.Y  * Side, LeadingEdgeOffset.Z);
	const FVector LocalTrailing = LocalForcePoint + FVector(TrailingEdgeOffset.X, TrailingEdgeOffset.Y * Side, TrailingEdgeOffset.Z);

	const FVector WorldLeading  = ActorTransform.TransformPosition(LocalLeading);
	const FVector WorldTrailing = ActorTransform.TransformPosition(LocalTrailing);

	if (bDrawDebugMarkers)
		DrawDebugLine(GetWorld(), WorldLeading, WorldTrailing, FColor::Black, false, -1, 0, 0.5f * bDebugMarkersSize);

	return (WorldLeading - WorldTrailing).GetSafeNormal();
}

FVector UFlightDynamicsComponent::FindAirflowDirection(FVector /*WorldOffset*/)
{
	const FVector ActorVelocity = Owner->GetVelocity();
	if (ActorVelocity.IsNearlyZero()) return FVector();
	return -ActorVelocity.GetSafeNormal();
}

FVector UFlightDynamicsComponent::FindLiftDirection(FVector /*WorldOffset*/, FVector AirflowDirection)
{
	const FVector WingRight = Root->GetRightVector();
	FVector LiftDirection = FVector::CrossProduct(AirflowDirection, WingRight);
	LiftDirection.Normalize();
	return LiftDirection.IsNearlyZero() ? FVector() : LiftDirection;
}

float UFlightDynamicsComponent::CalculateAoA(FVector AirflowDirection, FVector ChordDirection)
{
	if (!Owner->GetVelocity().IsNearlyZero())
	{
		const float Dot = FVector::DotProduct(ChordDirection, AirflowDirection);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	}
	return 0.0f;
}

void UFlightDynamicsComponent::LogMsg(FString Text)
{
	if (GEngine && DebugUILogs)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Black, Text);
	}
}
