#include "AttitudeControlComponent.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

THIRD_PARTY_INCLUDES_START
#include <zmq.hpp>
THIRD_PARTY_INCLUDES_END

// zmq.hpp drags in <windows.h> -> <wingdi.h>, which #defines OPAQUE.
// That clashes with CesiumGltf::Material's `static const std::string OPAQUE`
// member when both end up in the same unity translation unit.
#undef OPAQUE

// ─────────────────────────────────────────────────────────────────────────────
// ZMQ state — defined here so zmq.hpp never leaks into the header
// ─────────────────────────────────────────────────────────────────────────────

struct FZmqPullState
{
	zmq::context_t Context{ 1 };
	zmq::socket_t  Socket;

	explicit FZmqPullState(const FString& Endpoint)
		: Socket(Context, ZMQ_PULL)
	{
		Socket.bind(TCHAR_TO_UTF8(*Endpoint));
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Component
// ─────────────────────────────────────────────────────────────────────────────

UAttitudeControlComponent::UAttitudeControlComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UAttitudeControlComponent::BeginPlay()
{
	Super::BeginPlay();

	FlightDynamics = GetOwner()->FindComponentByClass<UFlightDynamicsComponent>();
	if (!FlightDynamics)
	{
		UE_LOG(LogTemp, Error, TEXT("AttitudeControlComponent: no UFlightDynamicsComponent found on %s"),
		       *GetOwner()->GetName());
		SetComponentTickEnabled(false);
		return;
	}

	try
	{
		ZmqState = new FZmqPullState(CommandEndpoint);
		UE_LOG(LogTemp, Log, TEXT("AttitudeControlComponent: ZMQ PULL bound to %s"), *CommandEndpoint);
	}
	catch (const zmq::error_t& E)
	{
		UE_LOG(LogTemp, Error, TEXT("AttitudeControlComponent: ZMQ bind failed — %hs"), E.what());
		SetComponentTickEnabled(false);
	}
}

void UAttitudeControlComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	delete ZmqState;
	ZmqState = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UAttitudeControlComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                               FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ZmqState || !FlightDynamics) return;

	PollCommands();
	ApplyAttitudeControl();
}

void UAttitudeControlComponent::PollCommands()
{
	zmq::message_t Msg;
	try
	{
		while (ZmqState->Socket.recv(&Msg, ZMQ_DONTWAIT))
		{
			const FString Json = UTF8_TO_TCHAR(static_cast<const char*>(Msg.data()));

			TSharedPtr<FJsonObject> Obj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) continue;

			FString CommandType;
			if (!Obj->TryGetStringField(TEXT("command_type"), CommandType)) continue;

			if (CommandType == TEXT("SET_ATTITUDE_TARGET"))
			{
				TargetRoll    = (float)Obj->GetNumberField(TEXT("roll"));
				TargetPitch   = (float)Obj->GetNumberField(TEXT("pitch"));
				TargetYawRate = (float)Obj->GetNumberField(TEXT("yaw_rate"));
				TargetThrust  = (float)Obj->GetNumberField(TEXT("thrust"));
			}
		}
	}
	catch (const zmq::error_t&)
	{
		// EAGAIN is expected when no messages are queued — ignore
	}
}

void UAttitudeControlComponent::ApplyAttitudeControl()
{
	const FRotator Rot = GetOwner()->GetActorRotation();
	const float CurrentRoll  = FMath::DegreesToRadians(Rot.Roll);
	const float CurrentPitch = FMath::DegreesToRadians(Rot.Pitch);

	float YawRateRadS = 0.f;
	if (UStaticMeshComponent* Mesh = GetOwner()->FindComponentByClass<UStaticMeshComponent>())
	{
		YawRateRadS = FMath::DegreesToRadians(Mesh->GetPhysicsAngularVelocityInDegrees().Z);
	}

	const float AileronCmd  = FMath::Clamp(RollGain    * (TargetRoll    - CurrentRoll),  -1.f, 1.f);
	const float ElevatorCmd = FMath::Clamp(PitchGain   * (TargetPitch   - CurrentPitch), -1.f, 1.f);
	const float RudderCmd   = FMath::Clamp(YawRateGain * (TargetYawRate - YawRateRadS),  -1.f, 1.f);

	FlightDynamics->UpdateAileronControl(AileronCmd, -AileronCmd);
	FlightDynamics->UpdateElevatorControl(ElevatorCmd, ElevatorCmd);
	FlightDynamics->UpdateRudderControl(RudderCmd);
	FlightDynamics->UpdateThrottleControl(TargetThrust);
}
