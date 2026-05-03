#include "SensorBusComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

THIRD_PARTY_INCLUDES_START
#include <zmq.hpp>
THIRD_PARTY_INCLUDES_END

// ─────────────────────────────────────────────────────────────────────────────
// ZMQ state — defined here so zmq.hpp never leaks into the header
// ─────────────────────────────────────────────────────────────────────────────

struct FZmqSocketState
{
	zmq::context_t Context{ 1 };
	zmq::socket_t  Socket;

	explicit FZmqSocketState(const FString& Endpoint)
		: Socket(Context, ZMQ_PUB)
	{
		int Hwm = 2;
		Socket.setsockopt(ZMQ_SNDHWM, &Hwm, sizeof(Hwm));
		Socket.bind(TCHAR_TO_UTF8(*Endpoint));
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Component
// ─────────────────────────────────────────────────────────────────────────────

USensorBusComponent::USensorBusComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USensorBusComponent::BeginPlay()
{
	Super::BeginPlay();

	try
	{
		ZmqState = new FZmqSocketState(Endpoint);
		UE_LOG(LogTemp, Log, TEXT("SensorBusComponent: ZMQ PUB bound to %s"), *Endpoint);
	}
	catch (const zmq::error_t& E)
	{
		UE_LOG(LogTemp, Error, TEXT("SensorBusComponent: ZMQ bind failed — %hs"), E.what());
		return;
	}

	// Build the sensor list: use explicit Sensors array or auto-discover on owner
	TArray<UActorComponent*> Resolved;
	if (Sensors.Num() > 0)
	{
		for (const TObjectPtr<UActorComponent>& Comp : Sensors)
		{
			if (Comp) Resolved.Add(Comp.Get());
		}
	}
	else if (AActor* Owner = GetOwner())
	{
		for (UActorComponent* Comp : Owner->GetComponents())
		{
			if (Comp && Comp->Implements<UUAVSensorInterface>())
				Resolved.Add(Comp);
		}
	}

	for (UActorComponent* Comp : Resolved)
	{
		if (!Cast<IUAVSensorInterface>(Comp))
		{
			UE_LOG(LogTemp, Warning, TEXT("SensorBusComponent: %s does not implement IUAVSensorInterface, skipped."),
				*Comp->GetName());
			continue;
		}
		ResolvedSensors.Add(Comp);
		UE_LOG(LogTemp, Log, TEXT("SensorBusComponent: registered sensor '%s' on %s"),
			*Cast<IUAVSensorInterface>(Comp)->GetSensorTopic(), *GetOwner()->GetName());
	}
}

void USensorBusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResolvedSensors.Empty();

	delete ZmqState;
	ZmqState = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void USensorBusComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	BusAccumulator += DeltaTime;
	const float BusInterval = 1.0f / FMath::Max(BusRate, 0.1f);
	if (BusAccumulator >= BusInterval)
	{
		BusAccumulator -= BusInterval;
		CollectAndSend();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// CollectAndSend — the core of the synchronous pipeline
// ─────────────────────────────────────────────────────────────────────────────

void USensorBusComponent::CollectAndSend()
{
	if (!ZmqState) return;

	// ── 1. Poll every sensor for its latest frame ─────────────────────────────
	TArray<FSensorFrame> Frames;
	for (const TWeakObjectPtr<UActorComponent>& WeakComp : ResolvedSensors)
	{
		UActorComponent* Comp = WeakComp.Get();
		if (!Comp) continue;

		IUAVSensorInterface* Sensor = Cast<IUAVSensorInterface>(Comp);
		if (!Sensor) continue;

		FSensorFrame Frame;
		if (Sensor->GetLatestFrame(Frame))
			Frames.Add(MoveTemp(Frame));
	}

	if (Frames.IsEmpty()) return;

	// ── 2. Build JSON envelope ────────────────────────────────────────────────
	// {"timestamp": <bus_time>, "sensors": [{"topic": "...", "timestamp": <sensor_time>}, ...]}
	TSharedRef<FJsonObject> EnvelopeObj = MakeShared<FJsonObject>();
	EnvelopeObj->SetNumberField(TEXT("timestamp"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);

	TArray<TSharedPtr<FJsonValue>> SensorEntries;
	for (const FSensorFrame& Frame : Frames)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("topic"),     Frame.Topic);
		Entry->SetNumberField(TEXT("timestamp"), Frame.Timestamp);
		SensorEntries.Add(MakeShared<FJsonValueObject>(Entry));
	}
	EnvelopeObj->SetArrayField(TEXT("sensors"), SensorEntries);

	FString EnvelopeStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&EnvelopeStr);
	FJsonSerializer::Serialize(EnvelopeObj, Writer);

	FTCHARToUTF8 EnvelopeUtf8(*EnvelopeStr);

	// ── 3. Send as a single atomic ZMQ multipart message ─────────────────────
	// Part 0:   JSON envelope
	// Part 1..N: raw payload for each sensor (same order as envelope "sensors" array)
	try
	{
		zmq::message_t EnvelopeMsg(EnvelopeUtf8.Get(), static_cast<size_t>(EnvelopeUtf8.Length()));
		ZmqState->Socket.send(EnvelopeMsg, ZMQ_SNDMORE | ZMQ_DONTWAIT);

		for (int32 i = 0; i < Frames.Num(); ++i)
		{
			const FSensorFrame& Frame   = Frames[i];
			const bool          bIsLast = (i == Frames.Num() - 1);
			const int           Flags   = bIsLast ? ZMQ_DONTWAIT : (ZMQ_SNDMORE | ZMQ_DONTWAIT);
			zmq::message_t PayloadMsg(Frame.Payload.GetData(), static_cast<size_t>(Frame.Payload.Num()));
			ZmqState->Socket.send(PayloadMsg, Flags);
		}
	}
	catch (const zmq::error_t&)
	{
		// Drop on HWM — receiver is too slow; don't block the game thread
	}
}
