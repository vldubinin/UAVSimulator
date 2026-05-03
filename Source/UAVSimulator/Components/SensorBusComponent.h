#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "SensorBusComponent.generated.h"

struct FZmqSocketState;

/**
 * Aggregates all IUAVSensorInterface components on the owner and publishes their frames
 * over a single ZMQ PUB socket as multipart messages: [topic][payload].
 *
 * Python client example:
 *   socket = context.socket(zmq.SUB)
 *   socket.connect("tcp://localhost:5555")
 *   socket.setsockopt_string(zmq.SUBSCRIBE, "camera")   # or "" for all topics
 *   topic, payload = socket.recv_multipart()
 *
 * If Sensors is left empty, all IUAVSensorInterface components on the owner are
 * discovered automatically at BeginPlay. Populate it explicitly to restrict the set.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API USensorBusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USensorBusComponent();

	/** ZMQ PUB endpoint. Python connects with zmq.SUB. Example: "tcp://*:5555" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	FString Endpoint = TEXT("tcp://*:5555");

	/**
	 * Explicit list of sensor components to aggregate (must implement IUAVSensorInterface).
	 * Leave empty to auto-discover all IUAVSensorInterface components on the owner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	TArray<TObjectPtr<UActorComponent>> Sensors;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Called on the game thread by any subscribed sensor's OnSensorDataReady delegate. */
	void OnSensorFrame(const FSensorFrame& Frame);

	/** Entry point for the dedicated ZMQ sender thread. */
	void SenderLoop();

	// ── ZMQ ──────────────────────────────────────────────────────────────────
	FZmqSocketState* ZmqState = nullptr;

	// ── Frame queue ───────────────────────────────────────────────────────────
	// Multiple sensors enqueue on the game thread; sender thread dequeues.
	TQueue<FSensorFrame, EQueueMode::Mpsc> PendingFrames;

	// ── Sender thread ─────────────────────────────────────────────────────────
	FEvent*          FrameReadyEvent = nullptr;
	FThreadSafeBool  bSenderRunning;
	FRunnable*       SenderRunnable  = nullptr;
	FRunnableThread* SenderThread    = nullptr;
};
