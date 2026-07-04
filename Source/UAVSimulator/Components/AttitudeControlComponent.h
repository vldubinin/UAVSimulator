#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AttitudeControlComponent.generated.h"

struct FZmqPullState;
class UFlightDynamicsComponent;

/**
 * Receives attitude commands from an external algorithm via ZMQ PULL socket
 * and converts them to control surface deflections using a proportional controller.
 *
 * Message format (JSON, UTF-8, plain send — not multipart):
 *   {"command_type":"SET_ATTITUDE_TARGET","roll":0.0,"pitch":0.0,"yaw_rate":0.0,"thrust":0.5}
 *
 * Algorithm side: zmq.PUSH → connect("tcp://localhost:5556")
 * Unreal side:    zmq.PULL ← bind("tcp://*:5556")
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UAttitudeControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAttitudeControlComponent();

	/** ZMQ PULL endpoint. Algorithm connects with zmq.PUSH. Example: "tcp://*:5556" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	FString CommandEndpoint = TEXT("tcp://*:5556");

	/** P-gain: roll angle error (rad) → aileron signal [-1, 1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control|Gains")
	float RollGain = 2.0f;

	/** P-gain: pitch angle error (rad) → elevator signal [-1, 1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control|Gains")
	float PitchGain = 2.0f;

	/** P-gain: yaw rate error (rad/s) → rudder signal [-1, 1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control|Gains")
	float YawRateGain = 1.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

private:
	void PollCommands();
	void ApplyAttitudeControl();

	FZmqPullState*           ZmqState      = nullptr;
	UFlightDynamicsComponent* FlightDynamics = nullptr;

	float TargetRoll    = 0.f;
	float TargetPitch   = 0.f;
	float TargetYawRate = 0.f;
	float TargetThrust  = 0.f;
};
