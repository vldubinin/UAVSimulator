#include "SensorBusComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "GameFramework/Actor.h"
#include "HAL/RunnableThread.h"

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
// Minimal FRunnable wrapper
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	class FLambdaRunnable final : public FRunnable
	{
	public:
		explicit FLambdaRunnable(TUniqueFunction<void()> InBody)
			: Body(MoveTemp(InBody)) {}
		virtual uint32 Run() override { Body(); return 0; }
	private:
		TUniqueFunction<void()> Body;
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// Component
// ─────────────────────────────────────────────────────────────────────────────

USensorBusComponent::USensorBusComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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

	FrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bSenderRunning  = true;
	SenderRunnable  = new FLambdaRunnable([this]() { SenderLoop(); });
	SenderThread    = FRunnableThread::Create(SenderRunnable, TEXT("UAV_SensorBusSender"), 0, TPri_BelowNormal);

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
		IUAVSensorInterface* Sensor = Cast<IUAVSensorInterface>(Comp);
		if (!Sensor)
		{
			UE_LOG(LogTemp, Warning, TEXT("SensorBusComponent: %s does not implement IUAVSensorInterface, skipped."),
				*Comp->GetName());
			continue;
		}
		Sensor->GetOnSensorDataReady().AddUObject(this, &USensorBusComponent::OnSensorFrame);
		UE_LOG(LogTemp, Log, TEXT("SensorBusComponent: subscribed to sensor '%s' on %s"),
			*Sensor->GetSensorTopic(), *GetOwner()->GetName());
	}
}

void USensorBusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unsubscribe from all sensors regardless of how Sensors was populated
	if (AActor* Owner = GetOwner())
	{
		for (UActorComponent* Comp : Owner->GetComponents())
		{
			if (IUAVSensorInterface* Sensor = Cast<IUAVSensorInterface>(Comp))
				Sensor->GetOnSensorDataReady().RemoveAll(this);
		}
	}

	if (SenderThread)
	{
		bSenderRunning = false;
		FrameReadyEvent->Trigger();
		SenderThread->WaitForCompletion();
		delete SenderThread;
		SenderThread = nullptr;
	}

	delete SenderRunnable;
	SenderRunnable = nullptr;

	if (FrameReadyEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(FrameReadyEvent);
		FrameReadyEvent = nullptr;
	}

	delete ZmqState;
	ZmqState = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Game-thread callback — enqueues frame for the sender thread
// ─────────────────────────────────────────────────────────────────────────────

void USensorBusComponent::OnSensorFrame(const FSensorFrame& Frame)
{
	if (!ZmqState || !bSenderRunning) return;
	PendingFrames.Enqueue(Frame);
	FrameReadyEvent->Trigger();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sender thread — dequeues frames and sends as ZMQ multipart [topic][payload]
// ─────────────────────────────────────────────────────────────────────────────

void USensorBusComponent::SenderLoop()
{
	while (bSenderRunning)
	{
		FrameReadyEvent->Wait(200);
		if (!bSenderRunning) break;

		FSensorFrame Frame;
		while (PendingFrames.Dequeue(Frame))
		{
			if (Frame.Payload.Num() == 0) continue;

			const auto TopicUtf8 = StringCast<ANSICHAR>(*Frame.Topic);
			try
			{
				zmq::message_t TopicMsg(TopicUtf8.Get(), static_cast<size_t>(TopicUtf8.Length()));
				zmq::message_t PayloadMsg(Frame.Payload.GetData(), static_cast<size_t>(Frame.Payload.Num()));
				ZmqState->Socket.send(TopicMsg, ZMQ_SNDMORE | ZMQ_DONTWAIT);
				ZmqState->Socket.send(PayloadMsg, ZMQ_DONTWAIT);
			}
			catch (const zmq::error_t&)
			{
				// Ignore EAGAIN (high-water mark reached)
			}
		}
	}
}
