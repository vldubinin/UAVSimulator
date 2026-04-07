#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileAndFlapRow.h"
#include "UAVSimulator/Entity/PolarRow.h"

/**
 * Orchestrates aerodynamic polar generation for aircraft surfaces.
 * File I/O, process execution, and polar parsing are delegated to AerodynamicToolRunner.
 */
class UAVSIMULATOR_API AerodynamicPhysicalCalculationUtil
{
public:
	/** Generate aerodynamic polars for all surfaces and store the results as DataTable assets. */
	static void GenerateAerodynamicPhysicalConfigutation(TArray<UAerodynamicSurfaceSC*> Surfaces);

	/** Run the OpenVSP/XFoil pipeline for a single surface segment and return the polar map. */
	static TMap<float, FPolarRow> CalculatePolar(FString PathToProfile,
		int32 RootChord, int32 TipChord, int32 Span,
		int32 DeflectionAngleStart, int32 DeflectionAngleEnd,
		int32 Sweep, FString SurfaceName, int32 SubSurfaceIndex);

	/** Find the .dat profile file associated with a surface's DataTable asset. */
	static FString FindPathToProfile(UAerodynamicSurfaceSC* Surface);
};
