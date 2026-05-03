#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SensorBusComponent.generated.h"

struct FZmqSocketState;

/**
 * Collects data from all IUAVSensorInterface components on the owner simultaneously
 * and publishes a single ZMQ multipart message each bus tick:
 *
 *   Part 0:   JSON envelope  — {"timestamp": T, "sensors": [{"topic": "camera", "timestamp": T1}, ...]}
 *   Part 1..N: Raw payloads  — one per sensor, in the same order as the envelope array
 *
 * Python client example:
 *   parts = socket.recv_multipart()
 *   envelope = json.loads(parts[0])
 *   for i, s in enumerate(envelope["sensors"], start=1):
 *       process(s["topic"], parts[i])
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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** ZMQ PUB endpoint. Python connects with zmq.SUB. Example: "tcp://*:5555" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	FString Endpoint = TEXT("tcp://*:5555");

	/** How many combined sensor packets are sent per second. Should match or exceed
	 *  the fastest sensor's rate (typically the camera's MaxEncodeFPS). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 0.1f, ClampMax = 120.0f))
	float BusRate = 30.0f;

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
	/** Polls every sensor, bundles results, sends one ZMQ multipart message. */
	void CollectAndSend();

	FZmqSocketState* ZmqState = nullptr;

	// Resolved at BeginPlay; iterated each bus tick
	TArray<TWeakObjectPtr<UActorComponent>> ResolvedSensors;

	float BusAccumulator = 0.0f;
};
