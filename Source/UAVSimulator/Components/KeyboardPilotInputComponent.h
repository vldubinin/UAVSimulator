#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/PilotInputSource.h"
#include "KeyboardPilotInputComponent.generated.h"

/**
 * Джерело керування: клавіатура. Реалізує IPilotInputSource (tier 0).
 *
 * Прив'язує legacy-осі KbdRoll/KbdPitch/KbdYaw/KbdThrottle (див. Config/DefaultInput.ini)
 * до обробників, що кешують сире значення. Клавіатура вже дає чіткі ±1/0, тож форма
 * (deadzone/expo) не застосовується — прямий прохід у FPilotCommand.
 *
 * Сам не тікає: legacy BindAxis-делегати спрацьовують під час обробки вводу актора,
 * а координатор (UPilotInputComponent) забирає значення через GetPilotCommand().
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UKeyboardPilotInputComponent : public UActorComponent, public IPilotInputSource
{
	GENERATED_BODY()

public:
	UKeyboardPilotInputComponent();

	// ── IPilotInputSource ────────────────────────────────────────────────────────
	virtual FName GetInputSourceId() const override { return FName(TEXT("keyboard")); }
	virtual int32 GetInputSourcePriority() const override { return 0; }
	virtual void BindInput(UInputComponent* InputComponent) override;
	virtual bool GetPilotCommand(FPilotCommand& OutCommand) override;

	/** Поточні сирі значення осей, оновлюються обробниками щокадру (0 коли клавішу відпущено). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Keyboard Input")
	float RawRoll = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Keyboard Input")
	float RawPitch = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Keyboard Input")
	float RawYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Keyboard Input")
	float RawThrottle = 0.f;

private:
	void OnRoll(float Value)     { RawRoll = Value; }
	void OnPitch(float Value)    { RawPitch = Value; }
	void OnYaw(float Value)      { RawYaw = Value; }
	void OnThrottle(float Value) { RawThrottle = Value; }
};
