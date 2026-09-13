#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SensorBusComponent.generated.h"

struct FZmqSocketState;

/**
 * Одночасно збирає дані з усіх компонентів IUAVSensorInterface на власнику
 * і публікує одне ZMQ multipart-повідомлення на кожному такті шини:
 *
 *   Частина 0:   JSON-конверт  — {"timestamp": T, "sensors": [{"topic": "camera", "timestamp": T1}, ...]}
 *   Частина 1..N: Сирі payload — по одному на датчик, у тому самому порядку, що й масив у конверті
 *
 * Приклад Python-клієнта:
 *   parts = socket.recv_multipart()
 *   envelope = json.loads(parts[0])
 *   for i, s in enumerate(envelope["sensors"], start=1):
 *       process(s["topic"], parts[i])
 *
 * Якщо Sensors залишити порожнім, усі компоненти IUAVSensorInterface на власнику
 * знаходяться автоматично в BeginPlay. Заповніть явно, щоб обмежити набір.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API USensorBusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USensorBusComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** ZMQ PUB endpoint. Python підключається через zmq.SUB. Приклад: "tcp://*:5555" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	FString Endpoint = TEXT("tcp://*:5555");

	/** Скільки об'єднаних пакетів датчиків надсилається за секунду. Має відповідати або
	 *  перевищувати частоту найшвидшого датчика (зазвичай MaxEncodeFPS камери). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 0.1f, ClampMax = 120.0f))
	float BusRate = 30.0f;

	/**
	 * Явний список компонентів-датчиків для агрегації (мають реалізувати IUAVSensorInterface).
	 * Залиште порожнім для автоматичного пошуку всіх компонентів IUAVSensorInterface на власнику.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	TArray<TObjectPtr<UActorComponent>> Sensors;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Опитує кожен датчик, об'єднує результати, надсилає одне ZMQ multipart-повідомлення. */
	void CollectAndSend();

	bool IsEnabledSensors();

	FZmqSocketState* ZmqState = nullptr;

	// Визначається в BeginPlay; перебирається на кожному такті шини
	TArray<TWeakObjectPtr<UActorComponent>> ResolvedSensors;

	float BusAccumulator = 0.0f;
};
