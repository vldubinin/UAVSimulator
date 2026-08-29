#include "KeyboardPilotInputComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Components/InputComponent.h"

UKeyboardPilotInputComponent::UKeyboardPilotInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UKeyboardPilotInputComponent::BindInput(UInputComponent* InputComponent)
{
	if (!InputComponent)
	{
		UE_LOG(LogUAV, Warning, TEXT("KeyboardPilotInputComponent::BindInput — InputComponent == null"));
		return;
	}

	InputComponent->BindAxis(TEXT("KbdRoll"),     this, &UKeyboardPilotInputComponent::OnRoll);
	InputComponent->BindAxis(TEXT("KbdPitch"),    this, &UKeyboardPilotInputComponent::OnPitch);
	InputComponent->BindAxis(TEXT("KbdYaw"),      this, &UKeyboardPilotInputComponent::OnYaw);
	InputComponent->BindAxis(TEXT("KbdThrottle"), this, &UKeyboardPilotInputComponent::OnThrottle);

	UE_LOG(LogUAV, Log, TEXT("KeyboardPilotInputComponent::BindInput — прив'язано осі Kbd{Roll,Pitch,Yaw,Throttle}"));
}

bool UKeyboardPilotInputComponent::GetPilotCommand(FPilotCommand& OutCommand)
{
	// Клавіатура вже чітка — без deadzone/expo, прямий прохід. Інверсію тангажу робить координатор.
	OutCommand.Roll             = RawRoll;
	OutCommand.Pitch            = RawPitch;
	OutCommand.Yaw              = RawYaw;
	OutCommand.ThrottleRate     = RawThrottle;
	OutCommand.ThrottleAbsolute = -1.f;
	OutCommand.bHasInput =
		!FMath::IsNearlyZero(RawRoll) || !FMath::IsNearlyZero(RawPitch) ||
		!FMath::IsNearlyZero(RawYaw)  || !FMath::IsNearlyZero(RawThrottle);

	return OutCommand.bHasInput;
}
