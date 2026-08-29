#include "PilotInputComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Interfaces/PilotInputSource.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/Engine.h"

UPilotInputComponent::UPilotInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPilotInputComponent::BeginPlay()
{
	Super::BeginPlay();

	FlightDynamics = GetOwner() ? GetOwner()->FindComponentByClass<UFlightDynamicsComponent>() : nullptr;
	if (!FlightDynamics)
	{
		UE_LOG(LogUAV, Error, TEXT("PilotInputComponent: не знайдено UFlightDynamicsComponent на %s — керування пілота вимкнено"),
			*GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}

	// Розв'язати джерела: явний список або авто-дискавер на власнику (патерн SensorBusComponent).
	TArray<UActorComponent*> Resolved;
	if (InputSources.Num() > 0)
	{
		for (const TObjectPtr<UActorComponent>& Comp : InputSources)
		{
			if (Comp) Resolved.Add(Comp.Get());
		}
	}
	else if (AActor* Owner = GetOwner())
	{
		for (UActorComponent* Comp : Owner->GetComponents())
		{
			if (Comp && Comp->Implements<UPilotInputSource>())
				Resolved.Add(Comp);
		}
	}

	for (UActorComponent* Comp : Resolved)
	{
		IPilotInputSource* Source = Cast<IPilotInputSource>(Comp);
		if (!Source) continue;

		ResolvedSources.Add(Comp);

		// Людські джерела вмикаємо тут; автопілот вмикає себе сам у ActivateAutopilot().
		const FName Id = Source->GetInputSourceId();
		if (Id == FName(TEXT("keyboard")) || Id == FName(TEXT("gamepad")))
		{
			Source->bInputSourceEnabled = true;
		}

		UE_LOG(LogUAV, Log, TEXT("PilotInputComponent: джерело '%s' (тир %d) %s"),
			*Id.ToString(), Source->GetInputSourcePriority(),
			Source->bInputSourceEnabled ? TEXT("увімкнено") : TEXT("вимкнено"));
	}

	UE_LOG(LogUAV, Log, TEXT("PilotInputComponent: розв'язано джерел %d на %s"),
		ResolvedSources.Num(), *GetNameSafe(GetOwner()));

	// Не збити встановлений у Blueprint/Details стартовий газ.
	Throttle01 = FMath::Clamp(FlightDynamics->TargetThrottle, 0.f, 1.f);
}

bool UPilotInputComponent::ShouldDrive() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled()) return false;
	return FlightDynamics != nullptr;
}

void UPilotInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ShouldDrive()) return;

	// 1. Зібрати команди активних джерел найвищого активного тиру.
	int32 MaxPrio = MIN_int32;
	TArray<FPilotCommand, TInlineAllocator<4>> Active;
	for (const TWeakObjectPtr<UActorComponent>& Weak : ResolvedSources)
	{
		UActorComponent* Comp = Weak.Get();
		if (!Comp) continue;

		IPilotInputSource* Source = Cast<IPilotInputSource>(Comp);
		if (!Source || !Source->bInputSourceEnabled) continue;

		FPilotCommand Cmd;
		if (!Source->GetPilotCommand(Cmd) || !Cmd.bHasInput) continue;

		const int32 Prio = Source->GetInputSourcePriority();
		if (Prio > MaxPrio) { MaxPrio = Prio; Active.Reset(); }
		if (Prio == MaxPrio) Active.Add(Cmd);
	}

	// 2. Звести (сума з обмеженням у межах тиру).
	float Roll = 0.f, Pitch = 0.f, Yaw = 0.f, ThrRate = 0.f, ThrAbs = -1.f;
	for (const FPilotCommand& Cmd : Active)
	{
		Roll    += Cmd.Roll;
		Pitch   += Cmd.Pitch;
		Yaw     += Cmd.Yaw;
		ThrRate += Cmd.ThrottleRate;
		if (Cmd.ThrottleAbsolute >= 0.f) ThrAbs = Cmd.ThrottleAbsolute;
	}
	Roll  = FMath::Clamp(Roll,  -1.f, 1.f);
	Pitch = FMath::Clamp(Pitch, -1.f, 1.f);
	Yaw   = FMath::Clamp(Yaw,   -1.f, 1.f);
	// Інверсія тангажу — властивість того, як ВІСЬ стика/клавіші лягає на елеватор, тож застосовна
	// лише до людського тиру (priority 0). Автопілот (tier 100) уже дає готову команду елеватора —
	// його інвертувати не можна. Через ексклюзивність тиру Active містить АБО людські джерела, АБО
	// автопілот, ніколи разом, тож перевірки MaxPrio достатньо.
	if (bInvertPitch && MaxPrio == 0) Pitch = -Pitch;

	// 3. Газ.
	if (ThrAbs >= 0.f)
	{
		Throttle01 = FMath::Clamp(ThrAbs, 0.f, 1.f);
	}
	else if (ThrottleModel == EPilotThrottleModel::Absolute)
	{
		Throttle01 = FMath::Clamp(ThrRate * 0.5f + 0.5f, 0.f, 1.f);
	}
	else
	{
		Throttle01 = FMath::Clamp(Throttle01 + ThrRate * ThrottleRampRate * DeltaTime, 0.f, 1.f);
	}

	// 4. Єдиний запис у динаміку польоту (форми — як у AttitudeControlComponent).
	FlightDynamics->UpdateAileronControl(Roll, -Roll);   // диференціал: L/R протилежні знаки
	FlightDynamics->UpdateElevatorControl(Pitch, Pitch); // елеватор: однаковий знак на обидві половини
	FlightDynamics->UpdateRudderControl(Yaw);
	FlightDynamics->UpdateThrottleControl(Throttle01);

	// 5. Дебаг.
	if (bLogInputDebug)
	{
		UE_LOG(LogUAV, Log,
			TEXT("%s: тир=%d | крен=%.2f тангаж=%.2f курс=%.2f | газ=%.2f (акт.джерел=%d)"),
			*GetNameSafe(GetOwner()), (MaxPrio == MIN_int32 ? 0 : MaxPrio), Roll, Pitch, Yaw, Throttle01, Active.Num());

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage((uint64)GetOwner()->GetUniqueID(), 0.7f, FColor::Yellow,
				FString::Printf(TEXT("%s  крен=%.2f тангаж=%.2f курс=%.2f  газ=%.2f"),
					*GetNameSafe(GetOwner()), Roll, Pitch, Yaw, Throttle01));
		}
	}
}
