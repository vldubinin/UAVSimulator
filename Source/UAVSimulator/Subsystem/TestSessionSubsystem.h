#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "TestSessionSubsystem.generated.h"

struct FTestSessionZmqState;
class UPrimitiveComponent;

/**
 * Сесія віртуальних льотних випробувань — міст між симулятором і Python-стендом
 * (Tools/TestingPlatform/flight_lab, пакет `uavlab`). Детально — Docs/14-Flight-Test-Lab.md.
 *
 * Два ZMQ-канали (окремо від сенсорної шини :5555 і команд автопілота :5556):
 *
 *  - REP `ControlEndpoint` (tcp://*:5557) — JSON-запит / JSON-відповідь `{"ok": bool, ...}`:
 *      {"command":"PING"}                         → {"ok":true,"protocol":1}
 *      {"command":"STATUS"}                       → {"ok":true,"running":..,"session_id":..,"sim_time":..,"mode":..}
 *      {"command":"GET_MANIFEST"}                 → {"ok":true,"manifest":{...}}
 *      {"command":"START","mode":"AutoTrack"}     → старт симуляції (mode/scenario_slot опційні)
 *      {"command":"STOP","reason":"time_limit"}   → зупинка + повернення в меню
 *
 *  - PUB `EventsEndpoint` (tcp://*:5558) — односкладові JSON-події:
 *      {"type":"session_start","session_id":..,"sim_time":..,"manifest":{...}}
 *      {"type":"session_end","session_id":..,"reason":"user|bench:<reason>",..,"duration_s":..}
 *      {"type":"event","kind":"collision",..}
 *
 * Старт/стоп можливі з обох боків: меню Unreal (Start / Q) теж проходить через
 * AUAVSimulatorGameModeBase::StartSimulation/StopSimulation, які викликають
 * NotifySimulationStarted/Stopped — тож стенд дізнається про них з подій.
 *
 * Сокети опитуються щотіку з ZMQ_DONTWAIT — ігровий потік ніколи не блокується.
 * Існує лише в Game/PIE світах.
 */
UCLASS()
class UAVSIMULATOR_API UTestSessionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ── UWorldSubsystem / FTickableGameObject ────────────────────────────────
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** ZMQ REP — команди від стенда. */
	FString ControlEndpoint = TEXT("tcp://*:5557");

	/** ZMQ PUB — події сесії для стенда. */
	FString EventsEndpoint = TEXT("tcp://*:5558");

	/** Мінімальний інтервал між подіями collision (фізика генерує hit щосабстепу під час ковзання). */
	float CollisionEventCooldownSeconds = 0.5f;

	/** Викликається з AUAVSimulatorGameModeBase::StartSimulation() ПІСЛЯ спавну й опанування акторів. */
	void NotifySimulationStarted();

	/** Викликається з AUAVSimulatorGameModeBase::StopSimulation() ДО знищення акторів. */
	void NotifySimulationStopped();

	/** Причина наступної зупинки (для session_end). Без виклику — "user" (Q / Start на геймпаді). */
	void SetPendingStopReason(const FString& Reason) { PendingStopReason = Reason; }

	bool IsSessionRunning() const { return bSessionRunning; }
	const FString& GetSessionId() const { return SessionId; }

	/** Повний опис умов тесту: режим, місце, оточення, сенсори, літаки, PID автопілота, фізика. */
	TSharedRef<FJsonObject> BuildManifest() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void PollControl();
	FString HandleRequest(const FString& RequestJson);
	void PublishEvent(const TSharedRef<FJsonObject>& Event);
	void BindCollisionEvents();

	UFUNCTION()
	void HandleAirplaneHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	                       UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	FTestSessionZmqState* ZmqState = nullptr;

	bool    bSessionRunning   = false;
	FString SessionId;
	FString PendingStopReason;
	double  SessionStartSimTime = 0.0;
	double  LastCollisionEventTime = -1.0e9;
	TSharedPtr<FJsonObject> SessionManifest;
};
