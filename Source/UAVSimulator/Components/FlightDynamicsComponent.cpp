// Fill out your copyright notice in the Description page of Project Settings.

#include "FlightDynamicsComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "UAVSimulator/Util/AerodynamicDebugRenderer.h"

UFlightDynamicsComponent::UFlightDynamicsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	PhysicsState = CreateDefaultSubobject<UUAVPhysicsStateComponent>(TEXT("PhysicsState"));
}

void UFlightDynamicsComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);

	ControlState = FControlInputState();
	Owner->GetComponents<UControlSurfaceSC>(ControlSurfaces);
	Owner->GetComponents<UAerodynamicSurfaceSC>(Surfaces);

	// Re-init with physics-accurate CoM (physics engine is active in BeginPlay)
	UStaticMeshComponent* Mesh = Owner->FindComponentByClass<UStaticMeshComponent>();
	FVector CoM = Mesh ? Mesh->GetCenterOfMass() : FVector::ZeroVector;
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		Surface->OnConstruction(CoM, ControlSurfaces);
	}

	if (InitialSpeedMs > 0.1f && Mesh)
	{
		FVector InitialVelocityUU = Owner->GetActorForwardVector() * (InitialSpeedMs * 100.0f);
		Mesh->SetPhysicsLinearVelocity(InitialVelocityUU);
	}
}

void UFlightDynamicsComponent::UpdateEditorVisualization(UStaticMeshComponent* Mesh)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	ControlState = FControlInputState();

	Surfaces.Empty();
	ControlSurfaces.Empty();
	Owner->GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	Owner->GetComponents<UControlSurfaceSC>(ControlSurfaces);

	FVector CoM = Mesh ? Mesh->GetCenterOfMass() : FVector::ZeroVector;
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		Surface->OnConstruction(CoM, ControlSurfaces);
	}

	if (Mesh)
	{
		AerodynamicDebugRenderer::DrawCrosshairs(Mesh, EngineThrustOffsetLocal, FName("ThrustPoint"));
		AerodynamicDebugRenderer::DrawLabel(Mesh,
			TEXT("Thrust Point"),
			EngineThrustOffsetLocal, FVector(0.f, 0.f, 50.f),
			FRotator(90.f, 180.f, 0.f), FColor::Green,
			FName("ThrustPointLabel"));
	}
}

void UFlightDynamicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	/* UE_LOG(LogUAV, Verbose, TEXT("### TICK ###")); */

	PhysicsState->Update();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	UStaticMeshComponent* Mesh = Owner->FindComponentByClass<UStaticMeshComponent>();
	if (Mesh && Mesh->IsSimulatingPhysics())
	{
		CurrentBoundVortices.Empty();

		const float SpeedMs = GetAirspeed();

		// Сумарна індуктивна сила від несучої лінії (для діагностики опору)
		FVector TotalInducedForceUU = FVector::ZeroVector;

		FAerodynamicForce TotalForce;
		for (UAerodynamicSurfaceSC* Surface : Surfaces)
		{
			FAerodynamicForce SurfaceForce = Surface->CalculateForcesOnSurface(
				PhysicsState->GetCenterOfMass(),
				PhysicsState->GetLinearVelocity(),
				PhysicsState->GetAngularVelocity(),
				PhysicsState->GetAirflowDirection(),
				ControlState,
				bVisualizeForces,
				DeltaTime);
			TotalForce.PositionalForce += SurfaceForce.PositionalForce;
			TotalForce.RotationalForce += SurfaceForce.RotationalForce;

			float SpanCm = 0.0f;
			for (const auto& Form : Surface->SurfaceForm)
			{
				SpanCm += FMath::Abs(Form.Offset.Y);
			}
			if (Surface->Mirror) SpanCm *= 2.0f;
			const float SpanM = SpanCm / 100.0f;

			// Кутта-Жуковський у зворотньому напрямку: Γ = F / (ρ * V * b)
			const float ForceN = SurfaceForce.PositionalForce.Size() / 100.0f;
			const float Gamma  = (SpeedMs > 0.1f && SpanM > 0.01f)
				? ForceN / (AirDensity * SpeedMs * SpanM)
				: 0.0f;

			/* UE_LOG(LogTemp, Warning, TEXT("Surface: %s | ForceN: %f | SpeedMs: %f | SpanM: %f | Calc Gamma: %f"),
				*Surface->GetName(), ForceN, SpeedMs, SpanM, Gamma); */

			const FVector SurfaceCenter = Surface->GetComponentLocation();
			const FVector RightDir      = Surface->GetRightVector();

			// Еліптичний розподіл циркуляції по розмаху (дискретна несуча лінія)
			const float GammaMax    = Gamma * (4.0f / UE_PI);
			const int32 NumSegments = FMath::Clamp(FMath::RoundToInt(SpanM / 0.5f), 2, 20);
			const float SegmentLen  = SpanCm / NumSegments;
			for (int32 j = 0; j < NumSegments; j++)
			{
				const float LocalY         = -SpanCm / 2.0f + SegmentLen * j;
				const float SegmentCenterY = LocalY + SegmentLen / 2.0f;
				const float HalfSpan       = SpanCm / 2.0f;
				const float LocalGamma     = (HalfSpan > 0.0f)
					? GammaMax * FMath::Sqrt(FMath::Max(0.0f, 1.0f - FMath::Pow(SegmentCenterY / HalfSpan, 2.0f)))
					: 0.0f;

				// --- ФІЗИКА ІНДУКТИВНОГО ОПОРУ ---
				FVector SegmentCenterCm = SurfaceCenter + RightDir * SegmentCenterY;
				FVector Vind_ms         = GetInducedVelocity(SegmentCenterCm);
				FVector SegmentDl_m     = (RightDir * SegmentLen) / 100.0f;

				// Теорема Кутти-Жуковського: F = rho * (Gamma * dl) x V
				FVector InducedForceN  = AirDensity * LocalGamma * FVector::CrossProduct(SegmentDl_m, Vind_ms);
				FVector InducedForceUU = InducedForceN * 100.0f;
				Mesh->AddForceAtLocation(InducedForceUU, SegmentCenterCm);
				TotalInducedForceUU += InducedForceUU;
				// ---------------------------------

				FBoundVortex BV;
				BV.StartPoint = SurfaceCenter + RightDir * LocalY;
				BV.EndPoint   = SurfaceCenter + RightDir * (LocalY + SegmentLen);
				BV.Gamma      = LocalGamma;
				CurrentBoundVortices.Add(BV);
			}
		}

		Mesh->AddForce(TotalForce.PositionalForce);
		Mesh->AddTorqueInRadians(TotalForce.RotationalForce);

		// Інерція розкручування двигуна
		CurrentThrottle = FMath::FInterpTo(CurrentThrottle, TargetThrottle, DeltaTime, EngineSpoolSpeed);

		// Значення для діагностичного логу (заповнюються нижче, якщо двигун працює)
		float LoggedThrustMultiplier = 0.0f;
		float LoggedActualThrustN    = 0.0f;

		if (CurrentThrottle > 0.01f)
		{
			float ThrustMultiplier = 1.0f;
			if (ThrustVsAirspeedCurve)
			{
				ThrustMultiplier = ThrustVsAirspeedCurve->GetFloatValue(SpeedMs);
			}
			else
			{
				/* UE_LOG(LogUAV, Warning, TEXT("ThrustVsAirspeedCurve не призначено — використовується множник 1.0")); */
			}

			const float ActualThrust        = MaxStaticThrust * CurrentThrottle * ThrustMultiplier;
			const FVector ThrustLocationWorld = Owner->GetActorTransform().TransformPosition(EngineThrustOffsetLocal);
			Mesh->AddForceAtLocation(Owner->GetActorForwardVector() * ActualThrust, ThrustLocationWorld);

			LoggedThrustMultiplier = ThrustMultiplier;
			LoggedActualThrustN    = ActualThrust / 100.0f;  // внутрішні одиниці (kg·cm/s²) → Н
		}

		// ── Діагностика розгону: справжня швидкість + баланс поздовжніх сил ──
		if (bLogFlightDebug)
		{
			DebugLogAccumulator += DeltaTime;
			if (DebugLogAccumulator >= 0.5f)
			{
				DebugLogAccumulator = 0.0f;

				const FVector VelCmS   = PhysicsState->GetLinearVelocity();   // cm/s
				const FVector VelDir   = VelCmS.GetSafeNormal();
				const float   SpeedKmh = VelCmS.Size() / 100.0f * 3.6f;
				const float   HorizKmh = FVector(VelCmS.X, VelCmS.Y, 0.f).Size() / 100.0f * 3.6f;
				const float   VertMs   = VelCmS.Z / 100.0f;

				// Розкладаємо сумарну аеродинамічну силу: опір = проекція на -V, підйом = складова +Z
				const FVector AeroN        = TotalForce.PositionalForce / 100.0f;                 // Н
				const float   DragN        = VelDir.IsNearlyZero() ? 0.f : -FVector::DotProduct(AeroN, VelDir);
				const float   LiftUpN      = AeroN.Z;
				const float   InducedDragN = VelDir.IsNearlyZero() ? 0.f : -FVector::DotProduct(TotalInducedForceUU / 100.0f, VelDir);

				const float   MassKg  = Mesh->GetMass();
				const float   WeightN = MassKg * 9.81f;

				UE_LOG(LogUAV, Warning,
					TEXT("[Розгін] %s | V=%.1f км/год (%.1f м/с) гор=%.1f верт=%.1f м/с | AoA=%.1f° | ")
					TEXT("газ ц/факт=%.2f/%.2f | Kтяги=%.3f Тяга=%.0f Н | опір поляри=%.0f Н індукт=%.0f Н разом=%.0f Н | ")
					TEXT("підйом=%.0f Н вага=%.0f Н | маса=%.1f кг"),
					*Owner->GetName(), SpeedKmh, VelCmS.Size() / 100.0f, HorizKmh, VertMs, GetAngleOfAttack(),
					TargetThrottle, CurrentThrottle, LoggedThrustMultiplier, LoggedActualThrustN,
					DragN, InducedDragN, DragN + InducedDragN, LiftUpN, WeightN, MassKg);

				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage((uint64)Owner->GetUniqueID(), 0.7f, FColor::Cyan,
						FString::Printf(TEXT("%s  V=%.1f км/год  AoA=%.1f°  газ=%.2f  Kтяги=%.2f  тяга=%.0f Н  опір=%.0f Н"),
							*Owner->GetName(), SpeedKmh, GetAngleOfAttack(), CurrentThrottle,
							LoggedThrustMultiplier, LoggedActualThrustN, DragN + InducedDragN));
				}
			}
		}

		/* UE_LOG(LogUAV, Log, TEXT("%s Location: %s"), *Owner->GetName(), *Owner->GetActorLocation().ToString()); */
	}

	UpdateVortexWake();

	// Скидаємо стан керування після застосування — нові значення надійдуть наступного тіку
	ControlState = FControlInputState();
}

void UFlightDynamicsComponent::UpdateAileronControl(float LeftAileronAngle, float RightAileronAngle)
{
	ControlState.LeftAileronAngle  = LeftAileronAngle;
	ControlState.RightAileronAngle = RightAileronAngle;
}

void UFlightDynamicsComponent::UpdateElevatorControl(float LeftElevatorAngle, float RightElevatorAngle)
{
	ControlState.LeftElevatorAngle  = LeftElevatorAngle;
	ControlState.RightElevatorAngle = RightElevatorAngle;
}

void UFlightDynamicsComponent::UpdateRudderControl(float RudderAngle)
{
	ControlState.RudderAngle = RudderAngle;
}

void UFlightDynamicsComponent::UpdateThrottleControl(float Throttle)
{
	TargetThrottle = FMath::Clamp(Throttle, 0.0f, 1.0f);
}

void UFlightDynamicsComponent::GenerateAerodynamicPhysicalConfigutation()
{
	AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(Surfaces);
}

void UFlightDynamicsComponent::UpdateVortexWake()
{
	// На кожен приєднаний вихор маємо 2 лінії сліду (корінь і кінцівка)
	if (VortexWakeLines.Num() != CurrentBoundVortices.Num() * 2)
	{
		VortexWakeLines.SetNum(CurrentBoundVortices.Num() * 2);
	}

	for (int32 i = 0; i < CurrentBoundVortices.Num(); i++)
	{
		const FBoundVortex& BV = CurrentBoundVortices[i];

		auto TryAddNode = [&](int32 LineIndex, FVector Pos, float NodeGamma)
		{
			TArray<FTrailingVortexNode>& Line = VortexWakeLines[LineIndex];
			if (Line.Num() == 0 || FVector::Distance(Line.Last().Position, Pos) >= MinWakeDistance)
			{
				FTrailingVortexNode Node;
				Node.Position = Pos;
				Node.Gamma    = NodeGamma;
				Line.Add(Node);
				if (Line.Num() > MaxWakeLength)
				{
					Line.RemoveAt(0);
				}
			}
		};

		// З кореня скидається +Gamma, з кінцівки скидається -Gamma (правило правої руки: сегмент іде Root→Tip)
		TryAddNode(i * 2,     BV.StartPoint,  BV.Gamma);
		TryAddNode(i * 2 + 1, BV.EndPoint,   -BV.Gamma);
	}
}

float UFlightDynamicsComponent::GetAngleOfAttack() const
{
	if (!PhysicsState) return 0.f;
	const FVector Vel = PhysicsState->GetLinearVelocity();
	if (Vel.IsNearlyZero()) return 0.f;
	AActor* Owner = GetOwner();
	if (!Owner) return 0.f;
	const float Dot = FVector::DotProduct(Owner->GetActorForwardVector(), Vel.GetSafeNormal());
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
}

FVector UFlightDynamicsComponent::GetLeftWingtipWorldPosition() const
{
	if (CurrentBoundVortices.Num() > 0)
		return CurrentBoundVortices[0].StartPoint;
	return FVector::ZeroVector;
}

FVector UFlightDynamicsComponent::GetInducedVelocity(const FVector& TargetPosCm) const
{
	FVector Vind_ms = FVector::ZeroVector;
	FVector P = TargetPosCm / 100.0f; // Перевід цільової точки у метри

	for (const auto& Line : VortexWakeLines)
	{
		for (int32 i = 0; i < Line.Num() - 1; i++)
		{
			FVector A = Line[i].Position / 100.0f;
			FVector B = Line[i + 1].Position / 100.0f;
			float NodeGamma = Line[i].Gamma;

			FVector R1 = P - A;
			FVector R2 = P - B;
			FVector R1xR2     = FVector::CrossProduct(R1, R2);
			float R1xR2_sqr   = R1xR2.SizeSquared();

			// Захист від сингулярності (щоб не ділити на нуль, якщо точка лежить на вихорі)
			if (R1xR2_sqr > 0.001f)
			{
				float r1  = R1.Size();
				float r2  = R2.Size();
				float Dot = FVector::DotProduct(R1, R2);

				FVector dV = (NodeGamma / (4.0f * UE_PI * R1xR2_sqr))
					* R1xR2
					* ((r1 + r2) * (1.0f - (Dot / (r1 * r2))));
				Vind_ms += dV;
			}
		}
	}
	return Vind_ms;
}
