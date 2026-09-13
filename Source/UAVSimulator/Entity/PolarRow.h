#pragma once

struct FPolarRow
{
	float CL = 0.0f;
	float CD = 0.0f;
	float CM = 0.0f;
};

// Псевдонім для сумісності — прибрати, коли всі виклики перейдуть на FPolarRow
using PolarRow = FPolarRow;
