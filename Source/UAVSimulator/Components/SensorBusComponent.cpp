#include "SensorBusComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"

THIRD_PARTY_INCLUDES_START
#include <zmq.hpp>
THIRD_PARTY_INCLUDES_END

// zmq.hpp тягне за собою <windows.h> -> <wingdi.h>, який визначає макрос OPAQUE.
// Це конфліктує з `static const std::string OPAQUE` — членом CesiumGltf::Material,
// коли обидва потрапляють у той самий unity translation unit.
#undef OPAQUE

// ─────────────────────────────────────────────────────────────────────────────
// Стан ZMQ — визначено тут, щоб zmq.hpp ніколи не потрапляв у заголовок
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
// Компонент
// ─────────────────────────────────────────────────────────────────────────────

USensorBusComponent::USensorBusComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USensorBusComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsEnabledSensors())
	{
		SetComponentTickEnabled(false);
		return;
	}

	try
	{
		ZmqState = new FZmqSocketState(Endpoint);
	}
	catch (const zmq::error_t& E)
	{
		UE_LOG(LogTemp, Error, TEXT("SensorBusComponent: ZMQ bind failed — %hs"), E.what());
		return;
	}

	// Побудова списку датчиків: явний масив Sensors або авто-дискавер на власнику
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
			continue;
		}
		ResolvedSensors.Add(Comp);
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
// Тік
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
// CollectAndSend — ядро синхронного пайплайну
// ─────────────────────────────────────────────────────────────────────────────

void USensorBusComponent::CollectAndSend()
{
	if (!ZmqState) return;

	// ── 1. Опитати кожен датчик на останній кадр ─────────────────────────────
	TArray<FSensorFrame> Frames;
	for (const TWeakObjectPtr<UActorComponent>& WeakComp : ResolvedSensors)
	{
		UActorComponent* Comp = WeakComp.Get();
		if (!Comp) continue;

		IUAVSensorInterface* Sensor = Cast<IUAVSensorInterface>(Comp);
		if (!Sensor || !Sensor->bSensorEnabled) continue;

		FSensorFrame Frame;
		if (Sensor->GetLatestFrame(Frame))
			Frames.Add(MoveTemp(Frame));
	}

	if (Frames.IsEmpty()) return;

	// ── 2. Побудувати JSON-конверт ────────────────────────────────────────────────
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

	// ── 3. Надіслати як одне атомарне ZMQ multipart-повідомлення ─────────────────────
	// Частина 0:   JSON-конверт
	// Частина 1..N: сирий payload кожного датчика (у тому самому порядку, що й масив "sensors" у конверті)
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
		// Втрата повідомлення при досягненні HWM — приймач надто повільний; не блокуємо ігровий потік
	}
}

bool USensorBusComponent::IsEnabledSensors()
{
	ESimulatorMode Mode;
	if (UUAVSimulationSubsystem* Subsystem = GetWorld()->GetSubsystem<UUAVSimulationSubsystem>())
	{
		Mode = Subsystem->CurrentSimulatorMode;
	}

	AActor* OwnerActor = GetOwner();
	bool IsPlayer = OwnerActor && OwnerActor->ActorHasTag(FName("Player"));

	if (Mode == ESimulatorMode::RecordTarget)
	{
		return false;
	}
	if (Mode == ESimulatorMode::PlaybackAndTrack && !IsPlayer) {
		return false;
	}
	return true;
}
