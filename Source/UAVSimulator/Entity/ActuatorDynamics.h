// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActuatorDynamics.generated.h"

/** Модель динаміки приводу керуючої поверхні. */
UENUM(BlueprintType)
enum class EActuatorDynamicsModel : uint8
{
	/** Лише обмеження швидкості відхилення (deg/s). Безумовно стійка модель за будь-якого DeltaTime. */
	RateLimitedOnly UMETA(DisplayName = "Обмеження швидкості"),
	/** Експоненційне згладжування (стала часу), додатково обмежене швидкістю. */
	FirstOrderLag UMETA(DisplayName = "Інерційна затримка (1-го порядку)"),
	/** Демпфована система 2-го порядку (маса-пружина-демпфер), додатково обмежена швидкістю. */
	CriticallyDampedSecondOrder UMETA(DisplayName = "Демпфована система 2-го порядку")
};

/**
 * Реалістична динаміка приводу керуючої поверхні: перетворює миттєвий командний кут
 * на фактичний фізичний кут з урахуванням перехідного процесу (швидкість приводу, інерція).
 * Виклик Advance() очікується один раз на тік з DeltaTime цього ж тіку.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FActuatorDynamics
{
	GENERATED_BODY()

	/** Модель перехідного процесу приводу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator", meta = (DisplayName = "Модель приводу"))
	EActuatorDynamicsModel Model = EActuatorDynamicsModel::RateLimitedOnly;

	/** Максимальна швидкість відхилення приводу, град/с. Діє як фізичне обмеження для всіх моделей. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator", meta = (DisplayName = "Макс. швидкість приводу (град/с)", ClampMin = "0.1"))
	float MaxSlewRateDegPerSec = 120.f;

	/** Стала часу експоненційної затримки (модель FirstOrderLag), секунди. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator", meta = (DisplayName = "Стала часу (с)", EditCondition = "Model == EActuatorDynamicsModel::FirstOrderLag", ClampMin = "0.001"))
	float TimeConstantSeconds = 0.15f;

	/** Власна частота коливань (модель CriticallyDampedSecondOrder), Гц. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator", meta = (DisplayName = "Власна частота (Гц)", EditCondition = "Model == EActuatorDynamicsModel::CriticallyDampedSecondOrder", ClampMin = "0.01"))
	float NaturalFrequencyHz = 2.0f;

	/** Коефіцієнт демпфування (1.0 = критичне демпфування, без перерегулювання). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator", meta = (DisplayName = "Коефіцієнт демпфування", EditCondition = "Model == EActuatorDynamicsModel::CriticallyDampedSecondOrder", ClampMin = "0.01"))
	float DampingRatio = 1.0f;

	/** Поточний фактичний кут приводу (град). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Actuator|Debug", meta = (DisplayName = "Поточний кут"))
	float Angle = 0.f;

	/** Поточна кутова швидкість приводу (град/с); використовується лише моделлю 2-го порядку. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Actuator|Debug", meta = (DisplayName = "Поточна швидкість"))
	float Velocity = 0.f;

	/**
	 * Просуває стан приводу на DeltaTime секунд у бік TargetAngle згідно обраної моделі.
	 * @return Новий фактичний кут приводу (град).
	 */
	float Advance(float TargetAngle, float DeltaTime);

	/** Скидає стан приводу до заданого початкового кута (наприклад, при ініціалізації в BeginPlay). */
	void Reset(float InitialAngle = 0.f);
};
