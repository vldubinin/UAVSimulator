#include "AttitudeControlComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

THIRD_PARTY_INCLUDES_START
#include <zmq.hpp>
THIRD_PARTY_INCLUDES_END

// zmq.hpp drags in <windows.h> -> <wingdi.h>, which #defines OPAQUE.
// That clashes with CesiumGltf::Material's `static const std::string OPAQUE`
// member when both end up in the same unity translation unit.
#undef OPAQUE

// ─────────────────────────────────────────────────────────────────────────────
// ZMQ state — defined here so zmq.hpp never leaks into the header
// ─────────────────────────────────────────────────────────────────────────────

struct FZmqPullState
{
	zmq::context_t Context{ 1 };
	zmq::socket_t  Socket;

	explicit FZmqPullState(const FString& Endpoint)
		: Socket(Context, ZMQ_PULL)
	{
		Socket.bind(TCHAR_TO_UTF8(*Endpoint));
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Component
// ─────────────────────────────────────────────────────────────────────────────

UAttitudeControlComponent::UAttitudeControlComponent()
{
	PrimaryComponentTick.bCanEverTick     = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // тікає лише після ActivateAutopilot()

	// Roll/Pitch регулюють КУТ — обертальна динаміка (момент керма -> кутове прискорення ->
	// кутова швидкість -> кут) це фактично подвійний інтегратор. Чистий П (Kd=0) на такому
	// об'єкті класично дає незгасаючі/наростаючі коливання (регулятор проскакує через 0 і
	// щоразу штовхає сильніше в інший бік) — звідси "задирає ніс -> валиться на крило" навіть
	// без жодної команди ззовні. Kd додає демпфування за швидкістю зміни кута. Це стартові
	// значення для тюнінгу — фінальні підбираються в PIE через Details-панель.
	RollPid.Kp = 2.0f;
	RollPid.Kd = 0.4f;
	RollPid.bIsAngularError = true;   // крен — кут, що проходить через ±180°

	PitchPid.Kp = 2.0f;
	PitchPid.Kd = 0.4f;
	PitchPid.bIsAngularError = true;  // тангаж — так само кут

	// YawRatePid регулює ШВИДКІСТЬ повороту, а не курсовий кут (одинарний інтегратор) —
	// тому чистого П тут достатньо для стійкості, Kd не потрібен.
	YawRatePid.Kp = 1.0f;
	YawRatePid.bIsAngularError = false; // це швидкість, не кут — обгортання не потрібне
}

void UAttitudeControlComponent::BeginPlay()
{
	Super::BeginPlay();
	// Свідомо нічого не робимо тут: компонент присутній на кожному літаку, але ZMQ-порт і тік
	// вмикаються лише явним викликом ActivateAutopilot() для того екземпляра, що справді має
	// керуватись автопілотом (див. коментар у .h).
}

void UAttitudeControlComponent::ActivateAutopilot()
{
	if (ZmqState) return; // вже активовано

	FlightDynamics = GetOwner()->FindComponentByClass<UFlightDynamicsComponent>();
	if (!FlightDynamics)
	{
		UE_LOG(LogUAV, Error, TEXT("AttitudeControlComponent: не знайдено UFlightDynamicsComponent на %s — автопілот не активовано"),
			*GetOwner()->GetName());
		return;
	}

	// Blueprint-графа акторного тіку (напр. Cessna_172) теж пише в
	// UpdateAileron/Elevator/Rudder/ThrottleControl (напр. колесо миші реасертовує
	// накопичений throttle щокадру незалежно від фактичного скролу). Порядок тіків
	// між нею та цим компонентом не гарантований, тож без явного prerequisite ручне
	// керування час від часу перебиває ZMQ-команди назад у 0. Тікаємо після власного
	// тіку актора, щоб команди автопілота завжди були останнім записом за кадр.
	AddTickPrerequisiteActor(GetOwner());

	try
	{
		ZmqState = new FZmqPullState(CommandEndpoint);
		SetComponentTickEnabled(true);
		bInputSourceEnabled = true; // тепер координатор (UPilotInputComponent) враховує цей тир
		UE_LOG(LogUAV, Log, TEXT("AttitudeControlComponent: автопілот активовано на %s, ZMQ PULL прив'язано до %s"),
			*GetOwner()->GetName(), *CommandEndpoint);
	}
	catch (const zmq::error_t& E)
	{
		UE_LOG(LogUAV, Error, TEXT("AttitudeControlComponent: ZMQ bind failed — %hs"), E.what());
	}
}

void UAttitudeControlComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bInputSourceEnabled = false;
	delete ZmqState;
	ZmqState = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UAttitudeControlComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                               FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ZmqState || !FlightDynamics) return;

	PollCommands();
	ComputeCommands(DeltaTime);
}

void UAttitudeControlComponent::PollCommands()
{
	zmq::message_t Msg;
	try
	{
		while (ZmqState->Socket.recv(&Msg, ZMQ_DONTWAIT))
		{
			// zmq::message_t НЕ гарантує null-термінований буфер — Msg.size() це точна довжина
			// корисного навантаження. Каст напряму в C-рядок (без явної довжини) читав би пам'ять
			// за межами повідомлення, доки випадково не натрапить на нульовий байт деінде в купі.
			const FUTF8ToTCHAR Converter(static_cast<const char*>(Msg.data()), static_cast<int32>(Msg.size()));
			const FString Json(Converter.Length(), Converter.Get());

			TSharedPtr<FJsonObject> Obj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
			{
				UE_LOG(LogUAV, Warning, TEXT("AttitudeControlComponent: не вдалося розпарсити вхідний JSON: %s"), *Json);
				continue;
			}

			FString CommandType;
			if (!Obj->TryGetStringField(TEXT("command_type"), CommandType))
			{
				UE_LOG(LogUAV, Warning, TEXT("AttitudeControlComponent: у вхідному повідомленні відсутнє поле command_type: %s"), *Json);
				continue;
			}

			if (CommandType == TEXT("SET_ATTITUDE_TARGET"))
			{
				TargetRoll    = (float)Obj->GetNumberField(TEXT("roll"));
				TargetPitch   = (float)Obj->GetNumberField(TEXT("pitch"));
				TargetYawRate = (float)Obj->GetNumberField(TEXT("yaw_rate"));
				TargetThrust  = (float)Obj->GetNumberField(TEXT("thrust"));

				UE_LOG(LogUAV, Log,
					TEXT("AttitudeControlComponent: вхідний сигнал SET_ATTITUDE_TARGET — roll=%.1f° pitch=%.1f° yaw_rate=%.1f°/s thrust=%.2f"),
					FMath::RadiansToDegrees(TargetRoll), FMath::RadiansToDegrees(TargetPitch),
					FMath::RadiansToDegrees(TargetYawRate), TargetThrust);
			}
			else
			{
				UE_LOG(LogUAV, Warning, TEXT("AttitudeControlComponent: невідомий command_type '%s'"), *CommandType);
			}
		}
	}
	catch (const zmq::error_t&)
	{
		// EAGAIN is expected when no messages are queued — ignore
	}
}

bool UAttitudeControlComponent::GetPilotCommand(FPilotCommand& OutCommand)
{
	OutCommand.Roll             = LastAileron;
	OutCommand.Pitch            = LastElevator;
	OutCommand.Yaw              = LastRudder;
	OutCommand.ThrottleRate     = 0.f;
	OutCommand.ThrottleAbsolute = LastThrust;     // автопілот задає газ абсолютно [0,1]
	OutCommand.bHasInput        = (ZmqState != nullptr); // активований => завжди володіє літаком
	return OutCommand.bHasInput;
}

void UAttitudeControlComponent::ComputeCommands(float DeltaTime)
{
	const FRotator Rot = GetOwner()->GetActorRotation();
	const float CurrentRoll  = FMath::DegreesToRadians(Rot.Roll);
	const float CurrentPitch = FMath::DegreesToRadians(Rot.Pitch);

	float YawRateRadS = 0.f;
	if (UStaticMeshComponent* Mesh = GetOwner()->FindComponentByClass<UStaticMeshComponent>())
	{
		YawRateRadS = FMath::DegreesToRadians(Mesh->GetPhysicsAngularVelocityInDegrees().Z);
	}

	// Гейн-шедулінг (за наявності налаштованої кривої) масштабує коефіцієнти за повітряною
	// швидкістю — аеродинамічний відгук керма зростає як V².
	const float Airspeed = FlightDynamics->GetAirspeed();

	const float AileronCmd  = RollPid.Update(TargetRoll, CurrentRoll, DeltaTime, Airspeed);
	const float ElevatorCmd = PitchPid.Update(TargetPitch, CurrentPitch, DeltaTime, Airspeed);
	const float RudderCmd   = YawRatePid.Update(TargetYawRate, YawRateRadS, DeltaTime, Airspeed);

	if (bLogAttitudeDebug)
	{
		UE_LOG(LogUAV, Log,
			TEXT("%s: Roll ціль=%.1f° поточ=%.1f° вихід=%.2f | Pitch ціль=%.1f° поточ=%.1f° вихід=%.2f | YawRate ціль=%.1f°/с поточ=%.1f°/с вихід=%.2f"),
			*GetOwner()->GetName(),
			FMath::RadiansToDegrees(TargetRoll),    FMath::RadiansToDegrees(CurrentRoll),    AileronCmd,
			FMath::RadiansToDegrees(TargetPitch),   FMath::RadiansToDegrees(CurrentPitch),   ElevatorCmd,
			FMath::RadiansToDegrees(TargetYawRate), FMath::RadiansToDegrees(YawRateRadS),    RudderCmd);
	}

	// Запис у FlightDynamics робить координатор (UPilotInputComponent) — тут лише кешуємо.
	// Диференціал елеронів (L, -L) координатор формує сам із скаляра Roll.
	LastAileron  = AileronCmd;
	LastElevator = ElevatorCmd;
	LastRudder   = RudderCmd;
	LastThrust   = TargetThrust;
}
