// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "PidController.generated.h"

/**
 * Гнучко конфігурований PID-регулятор загального призначення (Рівень 2 автопілота:
 * помилка положення/швидкості -> нормалізований керуючий сигнал [-1, 1]).
 * Update() очікується один раз на тік з DeltaTime цього ж тіку.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FPidController
{
	GENERATED_BODY()

	/** Пропорційний коефіцієнт. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Kp (пропорційний)"))
	float Kp = 1.f;

	/** Інтегральний коефіцієнт. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Ki (інтегральний)"))
	float Ki = 0.f;

	/** Диференціальний коефіцієнт. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Kd (диференціальний)"))
	float Kd = 0.f;

	/** Нижня межа вихідного сигналу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Мінімум виходу"))
	float OutputMin = -1.f;

	/** Верхня межа вихідного сигналу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Максимум виходу"))
	float OutputMax = 1.f;

	/** Анти-windup: межа вкладу I-складової у вихідний сигнал (в одиницях виходу). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Обмеження інтегральної складової", ClampMin = "0.0"))
	float IntegralClamp = 1.f;

	/** Стала часу НЧ-фільтра похідної складової, с. 0 — фільтр вимкнено. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Фільтр похідної (с)", ClampMin = "0.0"))
	float DerivativeFilterTimeConstant = 0.05f;

	/** Диференціювати вимірювання, а не похибку — уникає "кидка" при зміні уставки. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Похідна від вимірювання"))
	bool bDerivativeOnMeasurement = true;

	/** Похибка/дельта вимірювання — кутова величина, що проходить через ±180° (обгортати через UnwindRadians). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Кутова похибка (± 180°)"))
	bool bIsAngularError = false;

	/** Дозволяє вимкнути канал без втрати налаштувань (вихід форсовано 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Увімкнено"))
	bool bEnabled = true;

	/**
	 * Інвертує фінальний вихідний сигнал (P+I+D одразу після суми, до клемпування).
	 * Використовуй, якщо регулятор штовхає у ПРОТИЛЕЖНИЙ від потрібного бік — тобто помилку
	 * бачить правильно, але фізична відповідь цього конкретного літака/поверхні на команду
	 * протилежна очікуваній (додатний зворотний зв'язок замість від'ємного). Кожен літак може
	 * мати свою полярність тут — це нормально, не потребує зміни коду.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Інвертувати вихід"))
	bool bInvertOutput = false;

	/**
	 * Опційна крива гейн-шедулінгу: масштабує Kp/Ki/Kd за значенням ScheduleInput
	 * (типово — повітряна швидкість), бо аеродинамічний відгук масштабується як V².
	 * Порожня крива (без ключів) = множник 1 (гейн-шедулінг вимкнено).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID", meta = (DisplayName = "Крива гейн-шедулінгу"))
	FRuntimeFloatCurve GainScheduleCurve;

	// --- Стан регулятора (для спостереження під час тюнінгу в редакторі) ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PID|Debug")
	float Integral = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PID|Debug")
	float PreviousError = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PID|Debug")
	float PreviousMeasurement = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PID|Debug")
	float FilteredDerivative = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PID|Debug")
	float LastOutput = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PID|Debug")
	bool bHasPrevious = false;

	/**
	 * Обчислює черговий вихідний сигнал регулятора.
	 * @param Setpoint     — бажане значення.
	 * @param Measurement  — поточне виміряне значення.
	 * @param DeltaTime    — час з попереднього виклику, с.
	 * @param ScheduleInput — значення для GainScheduleCurve (типово повітряна швидкість, м/с).
	 * @return Вихідний сигнал у діапазоні [OutputMin, OutputMax].
	 */
	float Update(float Setpoint, float Measurement, float DeltaTime, float ScheduleInput = 0.f);

	/** Скидає весь стан регулятора (інтеграл, історію похідної, first-call guard). */
	void Reset();
};
