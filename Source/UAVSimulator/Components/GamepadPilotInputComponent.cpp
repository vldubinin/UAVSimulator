#include "GamepadPilotInputComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Components/InputComponent.h"

UGamepadPilotInputComponent::UGamepadPilotInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGamepadPilotInputComponent::BindInput(UInputComponent* InputComponent)
{
	if (!InputComponent)
	{
		UE_LOG(LogUAV, Warning, TEXT("GamepadPilotInputComponent::BindInput — InputComponent == null, осі не прив'язано"));
		return;
	}

	InputComponent->BindAxis(TEXT("PadRoll"),     this, &UGamepadPilotInputComponent::OnRoll);
	InputComponent->BindAxis(TEXT("PadPitch"),    this, &UGamepadPilotInputComponent::OnPitch);
	InputComponent->BindAxis(TEXT("PadYaw"),      this, &UGamepadPilotInputComponent::OnYaw);
	InputComponent->BindAxis(TEXT("PadThrottle"), this, &UGamepadPilotInputComponent::OnThrottle);

	UE_LOG(LogUAV, Log, TEXT("GamepadPilotInputComponent::BindInput — прив'язано осі Pad{Roll,Pitch,Yaw,Throttle} на %s"),
		*GetNameSafe(GetOwner()));
}

void UGamepadPilotInputComponent::LogRaw(const TCHAR* Axis, float Value) const
{
	if (bLogRawAxes && FMath::Abs(Value) > 0.02f)
	{
		UE_LOG(LogUAV, Log, TEXT("Gamepad сирий: %s = %.3f"), Axis, Value);
	}
}

float UGamepadPilotInputComponent::ShapeAxis(float Raw, float Deadzone, float Expo, float Sensitivity)
{
	const float A = FMath::Abs(Raw);
	if (A <= Deadzone) return 0.f;

	const float Sign = (Raw < 0.f) ? -1.f : 1.f;
	// Перемасштабування: край мертвої зони -> 0, повне відхилення все ще досягає 1 (без верхнього дедбенду).
	float M = (A - Deadzone) / FMath::Max(1.f - Deadzone, KINDA_SMALL_NUMBER);
	M = FMath::Lerp(M, M * M * M, Expo);

	return FMath::Clamp(Sign * M * Sensitivity, -1.f, 1.f);
}

bool UGamepadPilotInputComponent::GetPilotCommand(FPilotCommand& OutCommand)
{
	OutCommand.Roll         = ShapeAxis(RawRoll,  RollDeadzone,  RollExpo,  RollSensitivity);
	OutCommand.Pitch        = ShapeAxis(RawPitch, PitchDeadzone, PitchExpo, PitchSensitivity);
	OutCommand.Yaw          = ShapeAxis(RawYaw,   YawDeadzone,   YawExpo,   YawSensitivity);
	// Газ: без expo/чутливості — лише мертва зона; накопичення робить координатор.
	OutCommand.ThrottleRate = ShapeAxis(RawThrottle, ThrottleDeadzone, 0.f, 1.f);
	OutCommand.ThrottleAbsolute = -1.f;

	// Полярність осей цього пристрою.
	if (bInvertRoll)     OutCommand.Roll         = -OutCommand.Roll;
	if (bInvertPitch)    OutCommand.Pitch        = -OutCommand.Pitch;
	if (bInvertYaw)      OutCommand.Yaw          = -OutCommand.Yaw;
	if (bInvertThrottle) OutCommand.ThrottleRate = -OutCommand.ThrottleRate;

	OutCommand.bHasInput =
		!FMath::IsNearlyZero(OutCommand.Roll)  || !FMath::IsNearlyZero(OutCommand.Pitch) ||
		!FMath::IsNearlyZero(OutCommand.Yaw)   || !FMath::IsNearlyZero(OutCommand.ThrottleRate);

	return OutCommand.bHasInput;
}
