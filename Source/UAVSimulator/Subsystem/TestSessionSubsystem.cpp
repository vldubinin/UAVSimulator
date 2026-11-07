#include "TestSessionSubsystem.h"

#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/UAVSimulatorGameModeBase.h"
#include "UAVSimulator/UAVSimulatorPlayerController.h"
#include "UAVSimulator/Actor/Airplane.h"
#include "UAVSimulator/Actor/EWZoneActor.h"
#include "UAVSimulator/Actor/WindActor.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "UAVSimulator/Components/AttitudeControlComponent.h"
#include "UAVSimulator/Components/SensorBusComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Subsystem/UAVSimulationSubsystem.h"

#include "CesiumGeoreference.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

THIRD_PARTY_INCLUDES_START
#include <zmq.hpp>
THIRD_PARTY_INCLUDES_END

// zmq.hpp тягне <windows.h> -> <wingdi.h> з макросом OPAQUE, що конфліктує з
// CesiumGltf::Material::OPAQUE в unity-збірці (див. SensorBusComponent.cpp).
#undef OPAQUE

// ─────────────────────────────────────────────────────────────────────────────
// Стан ZMQ — у .cpp, щоб zmq.hpp не потрапляв у заголовок
// ─────────────────────────────────────────────────────────────────────────────

struct FTestSessionZmqState
{
	zmq::context_t Context{ 1 };
	zmq::socket_t  Control;
	zmq::socket_t  Events;

	FTestSessionZmqState(const FString& ControlEndpoint, const FString& EventsEndpoint)
		: Control(Context, ZMQ_REP)
		, Events(Context, ZMQ_PUB)
	{
		int Linger = 0;
		Control.setsockopt(ZMQ_LINGER, &Linger, sizeof(Linger));
		Events.setsockopt(ZMQ_LINGER, &Linger, sizeof(Linger));
		int Hwm = 100;
		Events.setsockopt(ZMQ_SNDHWM, &Hwm, sizeof(Hwm));

		Control.bind(TCHAR_TO_UTF8(*ControlEndpoint));
		Events.bind(TCHAR_TO_UTF8(*EventsEndpoint));
	}
};

namespace
{
	constexpr int32 ProtocolVersion = 1;

	FString ToJsonString(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	TSharedRef<FJsonObject> MakeReply(bool bOk, const FString& Error = FString())
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("ok"), bOk);
		if (!Error.IsEmpty()) R->SetStringField(TEXT("error"), Error);
		return R;
	}

	template <typename TEnum>
	FString EnumName(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	/** USaveGame-слот меню -> JSON (усі UPROPERTY) або nullptr, якщо слоту ще нема. */
	TSharedPtr<FJsonObject> SaveSlotToJson(const FString& SlotName)
	{
		if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0)) return nullptr;
		USaveGame* Save = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
		if (!Save) return nullptr;

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(Save->GetClass(), Save, Obj);
		return Obj;
	}

	TSharedRef<FJsonObject> StructToJson(const UStruct* Type, const void* Data)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(Type, Data, Obj);
		return Obj;
	}

	AUAVSimulatorGameModeBase* GetSimGameMode(const UWorld* World)
	{
		return World ? Cast<AUAVSimulatorGameModeBase>(World->GetAuthGameMode()) : nullptr;
	}

	AUAVSimulatorPlayerController* GetSimPlayerController(const UWorld* World)
	{
		return World ? Cast<AUAVSimulatorPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Життєвий цикл
// ─────────────────────────────────────────────────────────────────────────────

bool UTestSessionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTestSessionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	try
	{
		ZmqState = new FTestSessionZmqState(ControlEndpoint, EventsEndpoint);
		UE_LOG(LogUAV, Log, TEXT("TestSessionSubsystem: REP %s, PUB %s — стенд льотних випробувань може підключатись"),
			*ControlEndpoint, *EventsEndpoint);
	}
	catch (const zmq::error_t& E)
	{
		delete ZmqState;
		ZmqState = nullptr;
		UE_LOG(LogUAV, Error, TEXT("TestSessionSubsystem: ZMQ bind failed — %hs (інший екземпляр PIE?)"), E.what());
	}
}

void UTestSessionSubsystem::Deinitialize()
{
	delete ZmqState;
	ZmqState = nullptr;
	Super::Deinitialize();
}

TStatId UTestSessionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTestSessionSubsystem, STATGROUP_Tickables);
}

void UTestSessionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	PollControl();
}

// ─────────────────────────────────────────────────────────────────────────────
// Канал керування (REP)
// ─────────────────────────────────────────────────────────────────────────────

void UTestSessionSubsystem::PollControl()
{
	if (!ZmqState) return;

	try
	{
		zmq::message_t Request;
		// REP: строго recv -> send. Обробляємо по одному запиту за раз, поки черга не порожня.
		while (ZmqState->Control.recv(&Request, ZMQ_DONTWAIT))
		{
			const FUTF8ToTCHAR Converter(static_cast<const char*>(Request.data()), static_cast<int32>(Request.size()));
			const FString RequestJson(Converter.Length(), Converter.Get());

			const FString ReplyJson = HandleRequest(RequestJson);
			const FTCHARToUTF8 ReplyUtf8(*ReplyJson);
			zmq::message_t Reply(ReplyUtf8.Get(), static_cast<size_t>(ReplyUtf8.Length()));
			ZmqState->Control.send(Reply, 0);
		}
	}
	catch (const zmq::error_t& E)
	{
		if (E.num() != EAGAIN)
		{
			UE_LOG(LogUAV, Warning, TEXT("TestSessionSubsystem: помилка ZMQ REP — %hs"), E.what());
		}
	}
}

FString UTestSessionSubsystem::HandleRequest(const FString& RequestJson)
{
	TSharedPtr<FJsonObject> Req;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
	if (!FJsonSerializer::Deserialize(Reader, Req) || !Req.IsValid())
	{
		return ToJsonString(MakeReply(false, TEXT("invalid JSON")));
	}

	FString Command;
	Req->TryGetStringField(TEXT("command"), Command);
	Command = Command.ToUpper();

	UWorld* World = GetWorld();
	AUAVSimulatorGameModeBase* GM = GetSimGameMode(World);

	if (Command == TEXT("PING"))
	{
		TSharedRef<FJsonObject> R = MakeReply(true);
		R->SetNumberField(TEXT("protocol"), ProtocolVersion);
		return ToJsonString(R);
	}

	if (Command == TEXT("STATUS"))
	{
		TSharedRef<FJsonObject> R = MakeReply(true);
		R->SetBoolField(TEXT("running"), bSessionRunning);
		R->SetStringField(TEXT("session_id"), SessionId);
		R->SetNumberField(TEXT("sim_time"), World ? World->GetTimeSeconds() : 0.0);
		if (GM) R->SetStringField(TEXT("mode"), EnumName(GM->CurrentSimulatorMode));
		return ToJsonString(R);
	}

	if (Command == TEXT("GET_MANIFEST"))
	{
		TSharedRef<FJsonObject> R = MakeReply(true);
		R->SetObjectField(TEXT("manifest"), bSessionRunning && SessionManifest.IsValid() ? SessionManifest.ToSharedRef() : BuildManifest());
		return ToJsonString(R);
	}

	if (Command == TEXT("START"))
	{
		if (!GM) return ToJsonString(MakeReply(false, TEXT("GameMode is not AUAVSimulatorGameModeBase")));
		if (GM->bSimulationStarted) return ToJsonString(MakeReply(false, TEXT("simulation already running")));

		FString ModeStr;
		if (Req->TryGetStringField(TEXT("mode"), ModeStr) && !ModeStr.IsEmpty())
		{
			const int64 Value = StaticEnum<ESimulatorMode>()->GetValueByNameString(ModeStr);
			if (Value == INDEX_NONE)
			{
				return ToJsonString(MakeReply(false, FString::Printf(TEXT("unknown mode '%s'"), *ModeStr)));
			}
			GM->CurrentSimulatorMode = static_cast<ESimulatorMode>(Value);
		}

		FString Slot;
		if (Req->TryGetStringField(TEXT("scenario_slot"), Slot) && !Slot.IsEmpty())
		{
			GM->ScenarioSlotName = Slot;
		}

		const TSharedPtr<FJsonObject>* StartOverrides = nullptr;
		PendingPidOverrides = Req->TryGetObjectField(TEXT("pid_overrides"), StartOverrides) ? *StartOverrides : nullptr;

		if (AUAVSimulatorPlayerController* PC = GetSimPlayerController(World))
		{
			PC->StartSimulationAndHideMenu();
		}
		else
		{
			GM->StartSimulation();
		}

		bool bAutopilot = false;
		for (TActorIterator<AAirplane> It(World); It; ++It)
		{
			if (const UAttitudeControlComponent* AC = It->FindComponentByClass<UAttitudeControlComponent>())
			{
				bAutopilot |= AC->IsAutopilotActive();
			}
		}

		TSharedRef<FJsonObject> R = MakeReply(bSessionRunning, bSessionRunning ? FString() : TEXT("simulation did not start"));
		R->SetStringField(TEXT("session_id"), SessionId);
		R->SetStringField(TEXT("mode"), EnumName(GM->CurrentSimulatorMode));
		R->SetBoolField(TEXT("autopilot_active"), bAutopilot);
		return ToJsonString(R);
	}

	if (Command == TEXT("STOP"))
	{
		if (!GM || !GM->bSimulationStarted) return ToJsonString(MakeReply(false, TEXT("simulation is not running")));

		FString Reason;
		Req->TryGetStringField(TEXT("reason"), Reason);
		const FString StoppedId = SessionId;
		SetPendingStopReason(FString::Printf(TEXT("bench:%s"), Reason.IsEmpty() ? TEXT("stop") : *Reason));

		if (AUAVSimulatorPlayerController* PC = GetSimPlayerController(World))
		{
			PC->StopSimulationAndShowMenu();
		}
		else
		{
			GM->StopSimulation();
		}

		TSharedRef<FJsonObject> R = MakeReply(true);
		R->SetStringField(TEXT("session_id"), StoppedId);
		return ToJsonString(R);
	}

	if (Command == TEXT("SET_AUTOPILOT_PID"))
	{
		const TSharedPtr<FJsonObject>* Overrides = nullptr;
		if (!Req->TryGetObjectField(TEXT("pid_overrides"), Overrides))
		{
			return ToJsonString(MakeReply(false, TEXT("pid_overrides object is required")));
		}
		FString Error;
		const int32 Applied = ApplyPidOverrides(*Overrides, Error);
		if (!Error.IsEmpty())
		{
			return ToJsonString(MakeReply(false, Error));
		}
		if (bSessionRunning)
		{
			SessionManifest = BuildManifest();
		}

		TSharedRef<FJsonObject> Ev = MakeShared<FJsonObject>();
		Ev->SetStringField(TEXT("type"), TEXT("event"));
		Ev->SetStringField(TEXT("kind"), TEXT("pid_changed"));
		Ev->SetStringField(TEXT("session_id"), SessionId);
		Ev->SetNumberField(TEXT("sim_time"), World ? World->GetTimeSeconds() : 0.0);
		Ev->SetObjectField(TEXT("pid_overrides"), (*Overrides).ToSharedRef());
		PublishEvent(Ev);

		TSharedRef<FJsonObject> R = MakeReply(Applied > 0, Applied > 0 ? FString() : TEXT("no active autopilot"));
		R->SetNumberField(TEXT("applied_to"), Applied);
		return ToJsonString(R);
	}

	return ToJsonString(MakeReply(false, FString::Printf(TEXT("unknown command '%s'"), *Command)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Події (PUB)
// ─────────────────────────────────────────────────────────────────────────────

void UTestSessionSubsystem::PublishEvent(const TSharedRef<FJsonObject>& Event)
{
	if (!ZmqState) return;

	const FTCHARToUTF8 Utf8(*ToJsonString(Event));
	try
	{
		zmq::message_t Msg(Utf8.Get(), static_cast<size_t>(Utf8.Length()));
		ZmqState->Events.send(Msg, ZMQ_DONTWAIT);
	}
	catch (const zmq::error_t&)
	{
		// HWM — стенд не читає; подію втрачено, ігровий потік не блокуємо
	}
}

void UTestSessionSubsystem::NotifySimulationStarted()
{
	UWorld* World = GetWorld();
	const AUAVSimulatorGameModeBase* GM = GetSimGameMode(World);

	bSessionRunning     = true;
	SessionStartSimTime = World ? World->GetTimeSeconds() : 0.0;
	SessionId           = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))
	                    + TEXT("_") + (GM ? EnumName(GM->CurrentSimulatorMode) : FString(TEXT("Unknown")));
	PendingStopReason.Reset();
	LastCollisionEventTime = -1.0e9;

	if (PendingPidOverrides.IsValid())
	{
		FString Error;
		ApplyPidOverrides(PendingPidOverrides, Error);
		if (!Error.IsEmpty())
		{
			UE_LOG(LogUAV, Warning, TEXT("TestSessionSubsystem: pid_overrides зі START не застосовано — %s"), *Error);
		}
		PendingPidOverrides.Reset();
	}

	SessionManifest = BuildManifest();
	BindCollisionEvents();

	TSharedRef<FJsonObject> Ev = MakeShared<FJsonObject>();
	Ev->SetStringField(TEXT("type"), TEXT("session_start"));
	Ev->SetStringField(TEXT("session_id"), SessionId);
	Ev->SetNumberField(TEXT("sim_time"), SessionStartSimTime);
	Ev->SetObjectField(TEXT("manifest"), SessionManifest.ToSharedRef());
	PublishEvent(Ev);

	UE_LOG(LogUAV, Log, TEXT("TestSessionSubsystem: сесія %s розпочата"), *SessionId);
}

void UTestSessionSubsystem::NotifySimulationStopped()
{
	if (!bSessionRunning) return;

	const UWorld* World = GetWorld();
	const double  Now   = World ? World->GetTimeSeconds() : 0.0;
	const FString Reason = PendingStopReason.IsEmpty() ? FString(TEXT("user")) : PendingStopReason;

	TSharedRef<FJsonObject> Ev = MakeShared<FJsonObject>();
	Ev->SetStringField(TEXT("type"), TEXT("session_end"));
	Ev->SetStringField(TEXT("session_id"), SessionId);
	Ev->SetStringField(TEXT("reason"), Reason);
	Ev->SetNumberField(TEXT("sim_time"), Now);
	Ev->SetNumberField(TEXT("duration_s"), Now - SessionStartSimTime);
	PublishEvent(Ev);

	UE_LOG(LogUAV, Log, TEXT("TestSessionSubsystem: сесія %s завершена (%s)"), *SessionId, *Reason);

	bSessionRunning = false;
	PendingStopReason.Reset();
	SessionManifest.Reset();
}

int32 UTestSessionSubsystem::ApplyPidOverrides(const TSharedPtr<FJsonObject>& Overrides, FString& OutError)
{
	if (!Overrides.IsValid()) return 0;

	// Перевірка ключів до будь-яких змін — або все, або нічого.
	for (const auto& Pair : Overrides->Values)
	{
		if (Pair.Key != TEXT("roll") && Pair.Key != TEXT("pitch") && Pair.Key != TEXT("yaw_rate"))
		{
			OutError = FString::Printf(TEXT("unknown PID axis '%s' (roll | pitch | yaw_rate)"), *Pair.Key);
			return 0;
		}
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
		{
			OutError = FString::Printf(TEXT("PID axis '%s' must be an object"), *Pair.Key);
			return 0;
		}
	}

	auto Apply = [](FPidController& Pid, const TSharedPtr<FJsonObject>& O)
	{
		double V = 0.0;
		if (O->TryGetNumberField(TEXT("kp"), V)) Pid.Kp = static_cast<float>(V);
		if (O->TryGetNumberField(TEXT("ki"), V)) Pid.Ki = static_cast<float>(V);
		if (O->TryGetNumberField(TEXT("kd"), V)) Pid.Kd = static_cast<float>(V);
		if (O->TryGetNumberField(TEXT("integral_clamp"), V)) Pid.IntegralClamp = FMath::Max(0.f, static_cast<float>(V));
		if (O->TryGetNumberField(TEXT("derivative_filter_s"), V)) Pid.DerivativeFilterTimeConstant = FMath::Max(0.f, static_cast<float>(V));
		bool B = false;
		if (O->TryGetBoolField(TEXT("invert_output"), B)) Pid.bInvertOutput = B;
		FString Schedule;
		if (O->TryGetStringField(TEXT("gain_schedule"), Schedule) && Schedule == TEXT("disable")) Pid.DisableGainSchedule();
		Pid.Reset(); // інтеграл/похідна, накопичені зі старими коефіцієнтами, більше не валідні
	};

	int32 Applied = 0;
	for (TActorIterator<AAirplane> It(GetWorld()); It; ++It)
	{
		UAttitudeControlComponent* AC = It->FindComponentByClass<UAttitudeControlComponent>();
		if (!AC || !AC->IsAutopilotActive()) continue;

		const TSharedPtr<FJsonObject>* Axis = nullptr;
		if (Overrides->TryGetObjectField(TEXT("roll"), Axis))     Apply(AC->RollPid, *Axis);
		if (Overrides->TryGetObjectField(TEXT("pitch"), Axis))    Apply(AC->PitchPid, *Axis);
		if (Overrides->TryGetObjectField(TEXT("yaw_rate"), Axis)) Apply(AC->YawRatePid, *Axis);
		++Applied;
		UE_LOG(LogUAV, Log, TEXT("TestSessionSubsystem: PID автопілота %s змінено ззовні (лише на цю сесію)"), *It->GetName());
	}
	return Applied;
}

void UTestSessionSubsystem::BindCollisionEvents()
{
	for (TActorIterator<AAirplane> It(GetWorld()); It; ++It)
	{
		if (UStaticMeshComponent* Mesh = It->FindComponentByClass<UStaticMeshComponent>())
		{
			Mesh->SetNotifyRigidBodyCollision(true);
			Mesh->OnComponentHit.AddUniqueDynamic(this, &UTestSessionSubsystem::HandleAirplaneHit);
		}
	}
}

void UTestSessionSubsystem::HandleAirplaneHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                              UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bSessionRunning || !HitComponent) return;

	const UWorld* World = GetWorld();
	const double  Now   = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LastCollisionEventTime < CollisionEventCooldownSeconds) return;
	LastCollisionEventTime = Now;

	AActor* Owner = HitComponent->GetOwner();

	TSharedRef<FJsonObject> Ev = MakeShared<FJsonObject>();
	Ev->SetStringField(TEXT("type"), TEXT("event"));
	Ev->SetStringField(TEXT("kind"), TEXT("collision"));
	Ev->SetStringField(TEXT("session_id"), SessionId);
	Ev->SetNumberField(TEXT("sim_time"), Now);
	Ev->SetStringField(TEXT("actor"), Owner ? Owner->GetName() : FString());
	Ev->SetStringField(TEXT("other_actor"), OtherActor ? OtherActor->GetName() : FString());
	Ev->SetStringField(TEXT("other_class"), OtherActor ? OtherActor->GetClass()->GetName() : FString());
	// NormalImpulse — кг·см/с -> Н·с.
	Ev->SetNumberField(TEXT("impulse_ns"), NormalImpulse.Size() / 100.0);
	Ev->SetNumberField(TEXT("speed_mps"), HitComponent->GetPhysicsLinearVelocity().Size() / 100.0);
	PublishEvent(Ev);
}

// ─────────────────────────────────────────────────────────────────────────────
// Маніфест
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<FJsonObject> UTestSessionSubsystem::BuildManifest() const
{
	UWorld* World = GetWorld();
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetNumberField(TEXT("schema_version"), ProtocolVersion);
	M->SetStringField(TEXT("session_id"), SessionId);
	M->SetStringField(TEXT("created_utc"), FDateTime::UtcNow().ToIso8601());
	if (!World) return M;

	M->SetStringField(TEXT("level"), World->GetMapName());

	// ── Режим симуляції ───────────────────────────────────────────────────────
	if (const AUAVSimulatorGameModeBase* GM = GetSimGameMode(World))
	{
		TSharedRef<FJsonObject> Sim = MakeShared<FJsonObject>();
		Sim->SetStringField(TEXT("mode"), EnumName(GM->CurrentSimulatorMode));
		Sim->SetStringField(TEXT("scenario_slot"), GM->ScenarioSlotName);
		Sim->SetNumberField(TEXT("target_spawn_offset_m"), GM->TargetSpawnOffsetDistance / 100.0);
		Sim->SetStringField(TEXT("attitude_command_endpoint"), GM->AttitudeCommandEndpoint);
		Sim->SetStringField(TEXT("onboard_camera_mode"), EnumName(GM->OnboardCameraMode));
		Sim->SetStringField(TEXT("tracker_class"), GM->TrackerAirplaneClass ? GM->TrackerAirplaneClass->GetName() : FString());
		Sim->SetStringField(TEXT("target_class"), GM->TargetAirplaneClass ? GM->TargetAirplaneClass->GetName() : FString());
		M->SetObjectField(TEXT("simulation"), Sim);
	}

	// ── Фізика ────────────────────────────────────────────────────────────────
	if (const UPhysicsSettings* PS = UPhysicsSettings::Get())
	{
		TSharedRef<FJsonObject> Phys = MakeShared<FJsonObject>();
		Phys->SetBoolField(TEXT("substepping"), PS->bSubstepping);
		Phys->SetNumberField(TEXT("max_substeps"), PS->MaxSubsteps);
		Phys->SetNumberField(TEXT("max_substep_delta_time_s"), PS->MaxSubstepDeltaTime);
		M->SetObjectField(TEXT("physics"), Phys);
	}

	// ── Місце ─────────────────────────────────────────────────────────────────
	if (const ACesiumGeoreference* Geo = ACesiumGeoreference::GetDefaultGeoreference(World))
	{
		TSharedRef<FJsonObject> Loc = MakeShared<FJsonObject>();
		Loc->SetNumberField(TEXT("origin_latitude"),  Geo->GetOriginLatitude());
		Loc->SetNumberField(TEXT("origin_longitude"), Geo->GetOriginLongitude());
		Loc->SetNumberField(TEXT("origin_height_m"),  Geo->GetOriginHeight());
		M->SetObjectField(TEXT("location"), Loc);
	}

	// ── Налаштування меню (усі USaveGame-слоти секцій) ────────────────────────
	{
		TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
		static const TCHAR* Slots[][2] = {
			{ TEXT("scenario"),    TEXT("ScenarioSettings") },
			{ TEXT("sensors"),     TEXT("SensorSettings") },
			{ TEXT("environment"), TEXT("EnvironmentSettings") },
			{ TEXT("global"),      TEXT("GlobalSettings") },
		};
		for (const auto& Slot : Slots)
		{
			if (TSharedPtr<FJsonObject> Obj = SaveSlotToJson(Slot[1]))
			{
				Settings->SetObjectField(Slot[0], Obj);
			}
		}
		M->SetObjectField(TEXT("menu_settings"), Settings);
	}

	// ── Сенсори (фактично застосовані прапорці) + оточення ───────────────────
	if (const UUAVSimulationSubsystem* Sub = World->GetSubsystem<UUAVSimulationSubsystem>())
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("sensors_mode"), EnumName(Sub->SensorsMode));
		S->SetBoolField(TEXT("altimeter"),            Sub->bEnableSensorAltimeter);
		S->SetBoolField(TEXT("attitude_indicator"),   Sub->bEnableSensorAttitudeIndicator);
		S->SetBoolField(TEXT("camera_inclination"),   Sub->bEnableSensorCameraInclination);
		S->SetBoolField(TEXT("lidar"),                Sub->bEnableSensorLidar);
		S->SetBoolField(TEXT("camera"),               Sub->bEnableSensorCameraFrame);
		S->SetBoolField(TEXT("camera_altitude"),      Sub->bEnableSensorCameraAltitude);
		S->SetBoolField(TEXT("segmentation_mask"),    Sub->bEnableSensorSegmentationMask);
		S->SetBoolField(TEXT("bbox"),                 Sub->bEnableSensorBBoxDetection);
		S->SetBoolField(TEXT("drone_position"),       Sub->bEnableSensorPosition);
		S->SetBoolField(TEXT("drone_geo_position"),   Sub->bEnableSensorGeoPosition);
		S->SetBoolField(TEXT("cesium_objects"),       Sub->bEnableSensorCesiumSurroundings);
		S->SetBoolField(TEXT("custom_objects"),       Sub->bEnableSensorCustomSurroundings);
		S->SetBoolField(TEXT("flight_truth"),         Sub->bEnableSensorFlightTruth);
		M->SetObjectField(TEXT("sensor_flags"), S);

		TArray<TSharedPtr<FJsonValue>> Zones;
		for (const TWeakObjectPtr<AEWZoneActor>& Zone : Sub->EWZones)
		{
			if (!Zone.IsValid()) continue;
			TSharedRef<FJsonObject> Z = MakeShared<FJsonObject>();
			Z->SetStringField(TEXT("name"), Zone->GetName());
			Z->SetNumberField(TEXT("latitude"),  Zone->GetLatitude());
			Z->SetNumberField(TEXT("longitude"), Zone->GetLongitude());
			Z->SetNumberField(TEXT("radius_m"),  Zone->Radius / 100.0);
			Zones.Add(MakeShared<FJsonValueObject>(Z));
		}
		M->SetArrayField(TEXT("ew_zones"), Zones);

		TArray<TSharedPtr<FJsonValue>> Winds;
		for (const TWeakObjectPtr<AWindActor>& Wind : Sub->WindVectors)
		{
			if (!Wind.IsValid()) continue;
			TSharedRef<FJsonObject> W = MakeShared<FJsonObject>();
			W->SetStringField(TEXT("name"), Wind->GetName());
			const FVector Dir = Wind->GetActorForwardVector();
			const FVector Loc = Wind->GetActorLocation() / 100.0;
			W->SetNumberField(TEXT("speed_mps"), Wind->Speed / 100.0);
			W->SetNumberField(TEXT("radius_m"),  Wind->Radius / 100.0);
			W->SetArrayField(TEXT("direction_world"), { MakeShared<FJsonValueNumber>(Dir.X), MakeShared<FJsonValueNumber>(Dir.Y), MakeShared<FJsonValueNumber>(Dir.Z) });
			W->SetArrayField(TEXT("center_world_m"),  { MakeShared<FJsonValueNumber>(Loc.X), MakeShared<FJsonValueNumber>(Loc.Y), MakeShared<FJsonValueNumber>(Loc.Z) });
			Winds.Add(MakeShared<FJsonValueObject>(W));
		}
		M->SetArrayField(TEXT("wind_vectors"), Winds);
	}

	// ── Літаки ────────────────────────────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> Aircraft;
	for (TActorIterator<AAirplane> It(World); It; ++It)
	{
		AAirplane* Plane = *It;
		TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("name"), Plane->GetName());
		A->SetStringField(TEXT("class"), Plane->GetClass()->GetName());

		TArray<TSharedPtr<FJsonValue>> Tags;
		for (const FName& Tag : Plane->Tags) Tags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		A->SetArrayField(TEXT("tags"), Tags);

		A->SetNumberField(TEXT("actor_scale"), Plane->GetActorScale3D().X);
		A->SetObjectField(TEXT("calibration"), StructToJson(FAircraftCalibrationSettings::StaticStruct(), &Plane->CalibrationSettings));

		if (const UStaticMeshComponent* Mesh = Plane->FindComponentByClass<UStaticMeshComponent>())
		{
			A->SetNumberField(TEXT("mass_kg"), Mesh->IsSimulatingPhysics() ? Mesh->GetMass() : 0.0);
		}

		if (const UFlightDynamicsComponent* FD = Plane->FindComponentByClass<UFlightDynamicsComponent>())
		{
			TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
			const double DesignSpanM = FD->GetDesignWingSpanCm() / 100.0;
			F->SetNumberField(TEXT("design_wingspan_m"), DesignSpanM);
			F->SetNumberField(TEXT("wingspan_m"), DesignSpanM * Plane->GetActorScale3D().Y);
			F->SetNumberField(TEXT("max_static_thrust_n"), FD->GetMaxStaticThrust() / 100.0); // кг·см/с² -> Н
			F->SetNumberField(TEXT("engine_spool_speed"), FD->GetEngineSpoolSpeed());
			F->SetNumberField(TEXT("initial_speed_mps"), FD->InitialSpeedMs);
			F->SetNumberField(TEXT("air_density"), FD->AirDensity);
			F->SetNumberField(TEXT("debug_simulator_speed"), FD->DebugSimulatorSpeed);
			A->SetObjectField(TEXT("flight_dynamics"), F);
		}

		if (const UAttitudeControlComponent* AC = Plane->FindComponentByClass<UAttitudeControlComponent>())
		{
			TSharedRef<FJsonObject> AP = MakeShared<FJsonObject>();
			AP->SetBoolField(TEXT("active"), AC->IsAutopilotActive());
			AP->SetStringField(TEXT("command_endpoint"), AC->CommandEndpoint);
			AP->SetStringField(TEXT("note"), TEXT("Inner loop: roll/pitch angle PID (rad -> aileron/elevator [-1,1]), yaw-rate PID (rad/s -> rudder). Thrust is passed through as absolute throttle [0,1]."));
			AP->SetObjectField(TEXT("roll_pid"),     StructToJson(FPidController::StaticStruct(), &AC->RollPid));
			AP->SetObjectField(TEXT("pitch_pid"),    StructToJson(FPidController::StaticStruct(), &AC->PitchPid));
			AP->SetObjectField(TEXT("yaw_rate_pid"), StructToJson(FPidController::StaticStruct(), &AC->YawRatePid));
			TSharedRef<FJsonObject> Sched = MakeShared<FJsonObject>();
			Sched->SetBoolField(TEXT("roll"),     AC->RollPid.HasGainSchedule());
			Sched->SetBoolField(TEXT("pitch"),    AC->PitchPid.HasGainSchedule());
			Sched->SetBoolField(TEXT("yaw_rate"), AC->YawRatePid.HasGainSchedule());
			AP->SetObjectField(TEXT("gain_schedule_active"), Sched);
			A->SetObjectField(TEXT("autopilot"), AP);
		}

		if (const USensorBusComponent* Bus = Plane->FindComponentByClass<USensorBusComponent>())
		{
			TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
			B->SetStringField(TEXT("endpoint"), Bus->Endpoint);
			B->SetNumberField(TEXT("rate_hz"), Bus->BusRate);
			A->SetObjectField(TEXT("sensor_bus"), B);
		}

		TArray<TSharedPtr<FJsonValue>> Sensors;
		for (UActorComponent* Comp : Plane->GetComponents())
		{
			if (!Comp || !Comp->Implements<UUAVSensorInterface>()) continue;
			if (const IUAVSensorInterface* Sensor = Cast<IUAVSensorInterface>(Comp))
			{
				TSharedRef<FJsonObject> SObj = MakeShared<FJsonObject>();
				SObj->SetStringField(TEXT("topic"), Sensor->GetSensorTopic());
				SObj->SetStringField(TEXT("component"), Comp->GetClass()->GetName());
				SObj->SetBoolField(TEXT("enabled"), Sensor->bSensorEnabled);
				Sensors.Add(MakeShared<FJsonValueObject>(SObj));
			}
		}
		A->SetArrayField(TEXT("sensors"), Sensors);

		Aircraft.Add(MakeShared<FJsonValueObject>(A));
	}
	M->SetArrayField(TEXT("aircraft"), Aircraft);

	return M;
}
