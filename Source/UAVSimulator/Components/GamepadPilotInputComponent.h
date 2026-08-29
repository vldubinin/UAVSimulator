#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/PilotInputSource.h"
#include "GamepadPilotInputComponent.generated.h"

/**
 * Джерело керування: джойстик (тестується на 8BitDo SN30 Pro+ у режимі XInput). Реалізує
 * IPilotInputSource (tier 0). Розкладка RC Mode 2:
 *   права ручка X  -> крен   (PadRoll  <- Gamepad_RightX)
 *   права ручка Y  -> тангаж (PadPitch <- Gamepad_RightY)
 *   ліва ручка X   -> курс   (PadYaw   <- Gamepad_LeftX)
 *   ліва ручка Y   -> газ    (PadThrottle <- Gamepad_LeftY, інкрементно; акумулятор у координаторі)
 *
 * На відміну від клавіатури, стик потребує форми: deadzone -> expo -> sensitivity.
 * Сам не тікає — значення забирає координатор через GetPilotCommand().
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UGamepadPilotInputComponent : public UActorComponent, public IPilotInputSource
{
	GENERATED_BODY()

public:
	UGamepadPilotInputComponent();

	// ── IPilotInputSource ────────────────────────────────────────────────────────
	virtual FName GetInputSourceId() const override { return FName(TEXT("gamepad")); }
	virtual int32 GetInputSourcePriority() const override { return 0; }
	virtual void BindInput(UInputComponent* InputComponent) override;
	virtual bool GetPilotCommand(FPilotCommand& OutCommand) override;

	/** Мертва зона стика [0..0.9]: |вхід| <= deadzone дає 0. Свій, поверх (заниженого) двигунного. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Deadzone", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float RollDeadzone = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Deadzone", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float PitchDeadzone = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Deadzone", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float YawDeadzone = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Deadzone", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float ThrottleDeadzone = 0.05f;

	/** Крива відгуку: 0 = лінійно, 1 = кубічно (out = lerp(x, x^3, Expo)), знак збережено. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Expo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RollExpo = 0.40f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Expo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PitchExpo = 0.40f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Expo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float YawExpo = 0.50f;

	/** Масштаб чутливості після форми (клампиться до [-1,1]). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Sensitivity", meta = (ClampMin = "0.0"))
	float RollSensitivity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Sensitivity", meta = (ClampMin = "0.0"))
	float PitchSensitivity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Sensitivity", meta = (ClampMin = "0.0"))
	float YawSensitivity = 1.0f;

	// ── Інверсія сирих осей ЦЬОГО пристрою (полярність стика/тригера). Не плутати з
	//    UPilotInputComponent::bInvertPitch — той задає «куди = ніс угору» для всіх джерел.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Invert")
	bool bInvertRoll = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Invert")
	bool bInvertPitch = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Invert")
	bool bInvertYaw = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input|Invert")
	bool bInvertThrottle = false;

	/** Друкувати в лог сирі значення осей стика, коли вони ненульові (LogUAV), + факт BindInput. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gamepad Input")
	bool bLogRawAxes = false;

	/** Поточні сирі значення осей стика, оновлюються обробниками щокадру. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gamepad Input")
	float RawRoll = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gamepad Input")
	float RawPitch = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gamepad Input")
	float RawYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gamepad Input")
	float RawThrottle = 0.f;

private:
	/** deadzone -> перемасштабування -> expo -> sensitivity -> clamp[-1,1]. */
	static float ShapeAxis(float Raw, float Deadzone, float Expo, float Sensitivity);

	void LogRaw(const TCHAR* Axis, float Value) const;

	void OnRoll(float Value)     { RawRoll = Value;     LogRaw(TEXT("Roll(RightX)"),  Value); }
	void OnPitch(float Value)    { RawPitch = Value;    LogRaw(TEXT("Pitch(RightY)"), Value); }
	void OnYaw(float Value)      { RawYaw = Value;      LogRaw(TEXT("Yaw(LeftX)"),    Value); }
	void OnThrottle(float Value) { RawThrottle = Value; LogRaw(TEXT("Throttle(LeftY)"), Value); }
};
