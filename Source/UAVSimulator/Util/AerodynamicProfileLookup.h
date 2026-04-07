#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileRow.h"

/**
 * Encapsulates DataTable row lookup for aerodynamic polar profiles.
 * Row name format: FLAP_{angle}_Deg
 */
class UAVSIMULATOR_API AerodynamicProfileLookup
{
public:
	/**
	 * Find the aerodynamic profile row for the given flap deflection angle.
	 * Returns nullptr if the table is null or the row is missing.
	 */
	static FAerodynamicProfileRow* FindProfile(UDataTable* Table, int32 FlapAngle);
};
