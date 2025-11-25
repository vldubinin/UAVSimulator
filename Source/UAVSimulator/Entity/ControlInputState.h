#pragma once
struct ControlInputState
{
	float LeftAileronAngle;
	float RightAileronAngle;
	float LeftElevatorAngle;
	float RightElevatorAngle;
	float RudderAngle;

	ControlInputState()
		: LeftAileronAngle(0.f)
		, RightAileronAngle(0.f)
		, LeftElevatorAngle(0.f)
		, RightElevatorAngle(0.f)
		, RudderAngle(0.f)
	{ }
};