#pragma once

#include "UAVSimulator/Entity/FlapType.h"
#include "UAVSimulator/Entity/ControlInputState.h"

/**
 * Maps pilot control inputs to flap deflection angles.
 */
class UAVSIMULATOR_API ControlInputMapper
{
public:
	/**
	 * Map a normalized input signal [-1, 1] to a flap angle within [MinAngle, MaxAngle].
	 * The midpoint of the range corresponds to input = 0.
	 */
	static float MapInputToFlapAngle(float MinAngle, float MaxAngle, float InputSignal);

	/**
	 * Resolve the flap deflection angle for a given surface type, mirror flag, and current control state.
	 */
	static float ResolveFlapAngle(EFlapType FlapType, bool bIsMirror, float MinAngle, float MaxAngle, FControlInputState ControlState);
};
