#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Entity/PidController.h"
#include "UAVSimulator/Interfaces/PilotInputSource.h"
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
class UAVSIMULATOR_API UAttitudeControlComponent : public UActorComponent, public IPilotInputSource
{
	GENERATED_BODY()

public:
	UAttitudeControlComponent();

	// ── IPilotInputSource ────────────────────────────────────────────────────────
	// Автопілот — джерело найвищого (ексклюзивного) тиру: доки активований, повністю
	// перебиває клавіатуру/джойстик. Запис у FlightDynamics робить координатор
	// (UPilotInputComponent), цей компонент лише ОБЧИСЛЮЄ команди в ComputeCommands().
	virtual FName GetInputSourceId() const override { return FName(TEXT("autopilot")); }
	virtual int32 GetInputSourcePriority() const override { return 100; }
	virtual void BindInput(class UInputComponent* /*InputComponent*/) override {}
	virtual bool GetPilotCommand(FPilotCommand& OutCommand) override;

	/** ZMQ PULL endpoint. Algorithm connects with zmq.PUSH. Example: "tcp://*:5556" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	FString CommandEndpoint = TEXT("tcp://*:5556");

	/**
	 * Компонент присутній на кожному AAirplane (як CDO), але за замовчуванням неактивний —
	 * не тікає і не займає ZMQ-порт (усі літаки мають однаковий CommandEndpoint за замовчуванням,
	 * тож біндити його на кожному екземплярі одразу в BeginPlay було б помилкою). Викликається
	 * власником (напр. GameMode для Tracker-літака в PlaybackAndAutoTrack) щоб реально увімкнути
	 * автопілот на КОНКРЕТНОМУ екземплярі.
	 */
	void ActivateAutopilot();

	/** PID: roll angle error (rad) → aileron signal [-1, 1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control|PID")
	FPidController RollPid;

	/** PID: pitch angle error (rad) → elevator signal [-1, 1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control|PID")
	FPidController PitchPid;

	/** PID: yaw rate error (rad/s) → rudder signal [-1, 1] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control|PID")
	FPidController YawRatePid;

	/** Логувати в консоль уставку/поточний кут і вихід кожного PID щотіку. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	bool bLogAttitudeDebug = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

private:
	void PollCommands();
	/** Запускає PID за прийнятою уставкою і кешує результат у Last* (запис робить координатор). */
	void ComputeCommands(float DeltaTime);

	FZmqPullState*           ZmqState      = nullptr;
	UFlightDynamicsComponent* FlightDynamics = nullptr;

	float TargetRoll    = 0.f;
	float TargetPitch   = 0.f;
	float TargetYawRate = 0.f;
	float TargetThrust  = 0.f;

	// Останні обчислені команди — віддаються координатору через GetPilotCommand().
	float LastAileron  = 0.f;
	float LastElevator = 0.f;
	float LastRudder   = 0.f;
	float LastThrust   = 0.f;
};
