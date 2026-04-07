#include "ControlInputMapper.h"

float ControlInputMapper::MapInputToFlapAngle(float MinAngle, float MaxAngle, float InputSignal)
{
	float Mid = (MinAngle + MaxAngle) * 0.5f;
	return (InputSignal < 0.0f)
		? FMath::Lerp(Mid, MinAngle, -InputSignal)
		: FMath::Lerp(Mid, MaxAngle,  InputSignal);
}

float ControlInputMapper::ResolveFlapAngle(EFlapType FlapType, bool bIsMirror, float MinAngle, float MaxAngle, FControlInputState ControlState)
{
	switch (FlapType)
	{
	case EFlapType::Aileron:
		return MapInputToFlapAngle(MinAngle, MaxAngle,
			bIsMirror ? ControlState.RightAileronAngle : ControlState.LeftAileronAngle);

	case EFlapType::Elevator:
		return MapInputToFlapAngle(MinAngle, MaxAngle,
			bIsMirror ? ControlState.RightElevatorAngle : ControlState.LeftElevatorAngle);

	case EFlapType::Rudder:
		return MapInputToFlapAngle(MinAngle, MaxAngle, ControlState.RudderAngle);

	default:
		return 0.f;
	}
}
