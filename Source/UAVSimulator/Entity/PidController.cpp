#include "PidController.h"

float FPidController::Update(float Setpoint, float Measurement, float DeltaTime, float ScheduleInput)
{
	if (DeltaTime <= 0.f)
	{
		return LastOutput;
	}

	// Гейн-шедулінг: масштабуємо всі три коефіцієнти одним множником з кривої (порожня крива -> 1).
	const float ScheduleMultiplier = (GainScheduleCurve.GetRichCurveConst()->GetNumKeys() > 0)
		? GainScheduleCurve.GetRichCurveConst()->Eval(ScheduleInput)
		: 1.f;
	const float EffectiveKp = Kp * ScheduleMultiplier;
	const float EffectiveKi = Ki * ScheduleMultiplier;
	const float EffectiveKd = Kd * ScheduleMultiplier;

	float Error = Setpoint - Measurement;
	if (bIsAngularError)
	{
		Error = FMath::UnwindRadians(Error);
	}

	if (!bEnabled)
	{
		// Стежимо за станом навіть вимкненими, щоб повторне увімкнення не дало похідного "кидка".
		PreviousError       = Error;
		PreviousMeasurement = Measurement;
		bHasPrevious        = true;
		LastOutput          = 0.f;
		return 0.f;
	}

	const float PTerm = EffectiveKp * Error;

	// Анти-windup: клемп накладається на ВИХІДНИЙ вклад I-складової (в одиницях виходу),
	// а не на сиру суму — інакше межа "пливе" разом з гейн-шедулінгом.
	Integral += Error * DeltaTime;
	const float RawITerm = EffectiveKi * Integral;
	const float ITerm    = FMath::Clamp(RawITerm, -IntegralClamp, IntegralClamp);
	if (!FMath::IsNearlyZero(EffectiveKi))
	{
		Integral = ITerm / EffectiveKi;
	}

	float DTerm = 0.f;
	if (bHasPrevious)
	{
		float MeasurementDelta = Measurement - PreviousMeasurement;
		if (bIsAngularError)
		{
			MeasurementDelta = FMath::UnwindRadians(MeasurementDelta);
		}

		const float RawDerivative = bDerivativeOnMeasurement
			? -MeasurementDelta / DeltaTime
			: (Error - PreviousError) / DeltaTime;

		// НЧ-фільтр похідної (інакше вона підсилює шум фізичного солвера).
		const float Alpha = (DerivativeFilterTimeConstant > 0.f)
			? DeltaTime / (DerivativeFilterTimeConstant + DeltaTime)
			: 1.f;
		FilteredDerivative += Alpha * (RawDerivative - FilteredDerivative);
		DTerm = EffectiveKd * FilteredDerivative;
	}
	// Перший виклик (!bHasPrevious) навмисно лишає DTerm = 0 — інакше різниця з PreviousMeasurement=0
	// дала б спотворений "кидок" похідної на старті (наприклад, автопілот активується вже в крені).

	float RawOutput = PTerm + ITerm + DTerm;
	if (bInvertOutput)
	{
		RawOutput = -RawOutput;
	}
	const float Output = FMath::Clamp(RawOutput, OutputMin, OutputMax);

	PreviousError       = Error;
	PreviousMeasurement = Measurement;
	bHasPrevious        = true;
	LastOutput          = Output;

	return Output;
}

void FPidController::Reset()
{
	Integral            = 0.f;
	PreviousError       = 0.f;
	PreviousMeasurement = 0.f;
	FilteredDerivative  = 0.f;
	LastOutput          = 0.f;
	bHasPrevious        = false;
}
