#include "ControlInputMapper.h"

float ControlInputMapper::MapInputToFlapAngle(float MinAngle, float MaxAngle, float InputSignal)
{
	// Нейтральне положення (InputSignal = 0) відповідає середині діапазону
	float Mid = (MinAngle + MaxAngle) * 0.5f;
	// Від'ємний сигнал → інтерполяція від нейтралі до мінімуму
	// Додатний сигнал → інтерполяція від нейтралі до максимуму
	return (InputSignal < 0.0f)
		? FMath::Lerp(Mid, MinAngle, -InputSignal)
		: FMath::Lerp(Mid, MaxAngle,  InputSignal);
}

float ControlInputMapper::ResolveFlapAngle(EFlapType FlapType, bool bIsMirror, float MinAngle, float MaxAngle, FControlInputState ControlState)
{
	switch (FlapType)
	{
	case EFlapType::Aileron:
		// Дзеркальний елерон використовує правий канал (диференціальне відхилення)
		return MapInputToFlapAngle(MinAngle, MaxAngle,
			bIsMirror ? ControlState.RightAileronAngle : ControlState.LeftAileronAngle);

	case EFlapType::Elevator:
		// Дзеркальний руль висоти використовує правий канал
		return MapInputToFlapAngle(MinAngle, MaxAngle,
			bIsMirror ? ControlState.RightElevatorAngle : ControlState.LeftElevatorAngle);

	case EFlapType::Rudder:
		// Руль напрямку не диференціальний — один канал для обох половин
		return MapInputToFlapAngle(MinAngle, MaxAngle, ControlState.RudderAngle);

	default:
		return 0.f;
	}
}
