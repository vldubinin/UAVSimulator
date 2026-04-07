#pragma once

struct FPolarRow
{
	float CL = 0.0f;
	float CD = 0.0f;
	float CM = 0.0f;
};

// Compatibility alias — remove once all call sites use FPolarRow
using PolarRow = FPolarRow;
