#include "ActuatorDynamics.h"

float FActuatorDynamics::Advance(float TargetAngle, float DeltaTime)
{
	if (DeltaTime <= 0.f)
	{
		return Angle;
	}

	switch (Model)
	{
	case EActuatorDynamicsModel::RateLimitedOnly:
	{
		const float Delta   = TargetAngle - Angle;
		const float MaxStep = MaxSlewRateDegPerSec * DeltaTime;
		Angle = (FMath::Abs(Delta) <= MaxStep) ? TargetAngle : Angle + FMath::Sign(Delta) * MaxStep;
		Velocity = 0.f;
		break;
	}
	case EActuatorDynamicsModel::FirstOrderLag:
	{
		// Той самий прийом, що й розкрутка двигуна (FlightDynamicsComponent::CurrentThrottle) —
		// FMath::FInterpTo внутрішньо clamped і безумовно стійкий за будь-якого DeltaTime.
		const float Speed    = 1.0f / FMath::Max(TimeConstantSeconds, KINDA_SMALL_NUMBER);
		const float LagAngle = FMath::FInterpTo(Angle, TargetAngle, DeltaTime, Speed);
		const float MaxStep  = MaxSlewRateDegPerSec * DeltaTime;
		Angle += FMath::Clamp(LagAngle - Angle, -MaxStep, MaxStep);
		Velocity = 0.f;
		break;
	}
	case EActuatorDynamicsModel::CriticallyDampedSecondOrder:
	{
		// Напів-неявний (symplectic) метод Ейлера з під-кроками — явний Ейлер розходиться
		// при великому DeltaTime (наприклад DebugSimulatorSpeed >> 1) для "жвавих" приводів.
		const float MaxSubDt  = 1.0f / 240.0f;
		const int32 NumSteps  = FMath::Max(1, FMath::CeilToInt(DeltaTime / MaxSubDt));
		const float SubDt     = DeltaTime / NumSteps;
		const float Omega     = 2.0f * UE_PI * FMath::Max(NaturalFrequencyHz, KINDA_SMALL_NUMBER);

		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			const float Accel = Omega * Omega * (TargetAngle - Angle) - 2.0f * DampingRatio * Omega * Velocity;
			Velocity += Accel * SubDt;
			Velocity  = FMath::Clamp(Velocity, -MaxSlewRateDegPerSec, MaxSlewRateDegPerSec);
			Angle    += Velocity * SubDt;
		}
		break;
	}
	}

	return Angle;
}

void FActuatorDynamics::Reset(float InitialAngle)
{
	Angle    = InitialAngle;
	Velocity = 0.f;
}
