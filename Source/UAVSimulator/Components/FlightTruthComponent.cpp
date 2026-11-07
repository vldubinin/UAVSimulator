#include "FlightTruthComponent.h"

#include "CesiumGeoreference.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "UAVSimulator/Components/AttitudeControlComponent.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"
#include "UAVSimulator/Actor/EWZoneActor.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr double MetersPerDegLat = 111320.0;     // те саме наближення, що й у Python (gps_autopilote/common.py)
	constexpr double GravityCmS2     = 980.665;

	TSharedRef<FJsonObject> MakeVec(const FVector& V, const TCHAR* X = TEXT("x"), const TCHAR* Y = TEXT("y"), const TCHAR* Z = TEXT("z"))
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(X, V.X);
		O->SetNumberField(Y, V.Y);
		O->SetNumberField(Z, V.Z);
		return O;
	}

	/** Світова точка (см) -> (Lon, Lat, Height м) через геореференс (та сама послідовність, що в UGeoPositionDroneComponent). */
	FVector WorldToLLH(const ACesiumGeoreference* Geo, const FVector& WorldCm)
	{
		const FVector Local = Geo->GetActorTransform().InverseTransformPosition(WorldCm);
		return Geo->TransformUnrealPositionToLongitudeLatitudeHeight(Local);
	}

	/** Різниця двох LLH-точок у локальній площині ENU, метри. */
	FVector LLHDeltaToENU(const FVector& A, const FVector& B)
	{
		const double East  = (B.X - A.X) * FMath::Cos(FMath::DegreesToRadians(A.Y)) * MetersPerDegLat;
		const double North = (B.Y - A.Y) * MetersPerDegLat;
		const double Up    = B.Z - A.Z;
		return FVector(East, North, Up);
	}
}

UFlightTruthComponent::UFlightTruthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup    = TG_PostPhysics;
}

void UFlightTruthComponent::BeginPlay()
{
	Super::BeginPlay();

	Georeference = ACesiumGeoreference::GetDefaultGeoreference(this);

	if (AActor* Owner = GetOwner())
	{
		FlightDynamics  = Owner->FindComponentByClass<UFlightDynamicsComponent>();
		AttitudeControl = Owner->FindComponentByClass<UAttitudeControlComponent>();
		Mesh            = Owner->FindComponentByClass<UStaticMeshComponent>();
	}

	// Геттери FlightDynamics валідні лише після його тіку в поточному кадрі (див. CLAUDE.md).
	if (FlightDynamics)
	{
		AddTickPrerequisiteComponent(FlightDynamics);
	}
}

void UFlightTruthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled) return;

	CaptureState(DeltaTime);
}

void UFlightTruthComponent::CaptureState(float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (!Owner || !GetWorld()) return;

	const FVector  LocationCm = Owner->GetActorLocation();
	const FRotator Rot        = Owner->GetActorRotation();
	const FVector  Forward    = Owner->GetActorForwardVector();
	const FVector  Right      = Owner->GetActorRightVector();
	const FVector  Up         = Owner->GetActorUpVector();

	// Корінь актора не симулює фізику — швидкості беремо з фізичного тіла (як AAirplane::GetAirspeedMs).
	const FVector VelocityCmS    = Mesh ? Mesh->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const FVector AngVelWorldDeg = Mesh ? Mesh->GetPhysicsAngularVelocityInDegrees() : FVector::ZeroVector;
	const FVector AngVelBodyDeg  = Owner->GetActorTransform().InverseTransformVectorNoScale(AngVelWorldDeg);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("sim_time"), GetWorld()->GetTimeSeconds());

	// ── Позиція ───────────────────────────────────────────────────────────────
	Root->SetObjectField(TEXT("position_m"), MakeVec(LocationCm / 100.0));

	if (Georeference)
	{
		const FVector LLH = WorldToLLH(Georeference, LocationCm);
		TSharedRef<FJsonObject> Geo = MakeShared<FJsonObject>();
		Geo->SetNumberField(TEXT("latitude"),   LLH.Y);
		Geo->SetNumberField(TEXT("longitude"),  LLH.X);
		Geo->SetNumberField(TEXT("altitude_m"), LLH.Z);
		Root->SetObjectField(TEXT("geo"), Geo);

		// Швидкість і курс у ENU — через геореференс, щоб не залежати від орієнтації рівня.
		const FVector VelEnu = LLHDeltaToENU(LLH, WorldToLLH(Georeference, LocationCm + VelocityCmS));   // V·1с
		Root->SetObjectField(TEXT("velocity_enu_mps"), MakeVec(VelEnu, TEXT("east"), TEXT("north"), TEXT("up")));

		const FVector FwdEnu = LLHDeltaToENU(LLH, WorldToLLH(Georeference, LocationCm + Forward * 10000.0)); // 100 м уперед
		const double HeadingDeg = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(FwdEnu.X, FwdEnu.Y)) + 360.0, 360.0);
		Root->SetNumberField(TEXT("heading_deg"), HeadingDeg);

		const double GroundSpeed = FMath::Sqrt(VelEnu.X * VelEnu.X + VelEnu.Y * VelEnu.Y);
		Root->SetNumberField(TEXT("ground_speed_mps"), GroundSpeed);
		Root->SetNumberField(TEXT("vertical_speed_mps"), VelEnu.Z);
		if (GroundSpeed > 0.5)
		{
			Root->SetNumberField(TEXT("course_deg"),
				FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(VelEnu.X, VelEnu.Y)) + 360.0, 360.0));
		}
	}

	Root->SetObjectField(TEXT("velocity_mps"), MakeVec(VelocityCmS / 100.0));

	// ── Орієнтація й кутові швидкості ─────────────────────────────────────────
	{
		TSharedRef<FJsonObject> Att = MakeShared<FJsonObject>();
		Att->SetNumberField(TEXT("roll"),  Rot.Roll);
		Att->SetNumberField(TEXT("pitch"), Rot.Pitch);
		Att->SetNumberField(TEXT("yaw"),   Rot.Yaw);
		Root->SetObjectField(TEXT("attitude_deg"), Att);
	}
	// p — навколо поздовжньої осі (X), q — поперечної (Y), r — вертикальної (Z), тіло, °/с.
	Root->SetObjectField(TEXT("body_rates_dps"), MakeVec(AngVelBodyDeg, TEXT("p"), TEXT("q"), TEXT("r")));
	// Той самий сигнал, що порівнює YawRatePid автопілота (світова Z-компонента).
	Root->SetNumberField(TEXT("yaw_rate_world_dps"), AngVelWorldDeg.Z);

	// ── Аеродинаміка ──────────────────────────────────────────────────────────
	const double Speed = VelocityCmS.Size();
	if (FlightDynamics)
	{
		Root->SetNumberField(TEXT("airspeed_mps"), FlightDynamics->GetAirspeed());
		Root->SetNumberField(TEXT("aoa_deg"),      FlightDynamics->GetAngleOfAttack());
		Root->SetNumberField(TEXT("throttle"),     FlightDynamics->CurrentThrottle);
		Root->SetNumberField(TEXT("thrust_n"),     FlightDynamics->CurrentThrustN);
	}
	if (Speed > 1.0)
	{
		const double Sideslip = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(VelocityCmS, Right),
		                                                             FVector::DotProduct(VelocityCmS, Forward)));
		Root->SetNumberField(TEXT("sideslip_deg"), Sideslip);
	}

	// Перевантаження вздовж вертикальної осі тіла: n = (a + g)·Up / g.
	if (bHasPrevVelocity && DeltaTime > KINDA_SMALL_NUMBER)
	{
		const FVector Accel = (VelocityCmS - PrevVelocityCmS) / DeltaTime;
		const double  N     = FVector::DotProduct(Accel + FVector(0, 0, GravityCmS2), Up) / GravityCmS2;
		Root->SetNumberField(TEXT("load_factor_g"), N);
	}
	PrevVelocityCmS  = VelocityCmS;
	bHasPrevVelocity = true;

	// ── Автопілот (внутрішній PID-контур) ─────────────────────────────────────
	if (AttitudeControl)
	{
		TSharedRef<FJsonObject> AP = MakeShared<FJsonObject>();
		AP->SetBoolField(TEXT("active"), AttitudeControl->IsAutopilotActive());

		TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
		Target->SetNumberField(TEXT("roll_deg"),       FMath::RadiansToDegrees(AttitudeControl->GetTargetRoll()));
		Target->SetNumberField(TEXT("pitch_deg"),      FMath::RadiansToDegrees(AttitudeControl->GetTargetPitch()));
		Target->SetNumberField(TEXT("yaw_rate_dps"),   FMath::RadiansToDegrees(AttitudeControl->GetTargetYawRate()));
		Target->SetNumberField(TEXT("thrust"),         AttitudeControl->GetTargetThrust());
		AP->SetObjectField(TEXT("target"), Target);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("aileron"),  AttitudeControl->GetLastAileronCmd());
		Out->SetNumberField(TEXT("elevator"), AttitudeControl->GetLastElevatorCmd());
		Out->SetNumberField(TEXT("rudder"),   AttitudeControl->GetLastRudderCmd());
		AP->SetObjectField(TEXT("pid_output"), Out);

		// Множник кривої гейн-шедулінгу на поточній швидкості: ефективний Kp = Kp × множник.
		if (FlightDynamics)
		{
			const float V = FlightDynamics->GetAirspeed();
			TSharedRef<FJsonObject> Sched = MakeShared<FJsonObject>();
			Sched->SetNumberField(TEXT("roll"),     AttitudeControl->RollPid.GetScheduleMultiplier(V));
			Sched->SetNumberField(TEXT("pitch"),    AttitudeControl->PitchPid.GetScheduleMultiplier(V));
			Sched->SetNumberField(TEXT("yaw_rate"), AttitudeControl->YawRatePid.GetScheduleMultiplier(V));
			AP->SetObjectField(TEXT("gain_schedule_multiplier"), Sched);
		}

		const double LastCmd = AttitudeControl->GetLastCommandWorldTime();
		if (LastCmd >= 0.0)
		{
			AP->SetNumberField(TEXT("last_command_age_s"), GetWorld()->GetTimeSeconds() - LastCmd);
		}
		AP->SetNumberField(TEXT("commands_received"), AttitudeControl->GetReceivedCommandCount());
		Root->SetObjectField(TEXT("autopilot"), AP);
	}

	// ── Оточення ──────────────────────────────────────────────────────────────
	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		TSharedRef<FJsonObject> Env = MakeShared<FJsonObject>();
		const FVector Wind = Subsystem->GetWindVelocityAtLocation(LocationCm) / 100.0;
		Env->SetObjectField(TEXT("wind_mps"), MakeVec(Wind));
		Env->SetNumberField(TEXT("wind_speed_mps"), Wind.Size());

		float EW = 0.f;
		for (const TWeakObjectPtr<AEWZoneActor>& Zone : Subsystem->EWZones)
		{
			if (Zone.IsValid()) EW = FMath::Max(EW, Zone->GetInterferenceIntensity(LocationCm));
		}
		Env->SetNumberField(TEXT("ew_intensity"), EW);
		Root->SetObjectField(TEXT("environment"), Env);
	}

	LatestState     = Root;
	LatestTimestamp = GetWorld()->GetTimeSeconds();
}

bool UFlightTruthComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!LatestState.IsValid()) return false;

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(LatestState.ToSharedRef(), Writer);

	FTCHARToUTF8 Utf8(*JsonString);

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestTimestamp;
	OutFrame.Payload.Reset();
	OutFrame.Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return true;
}
