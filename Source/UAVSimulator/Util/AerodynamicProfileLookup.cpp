#include "AerodynamicProfileLookup.h"
#include "UAVSimulator/UAVSimulator.h"

FAerodynamicProfileRow* AerodynamicProfileLookup::FindProfile(UDataTable* Table, int32 FlapAngle)
{
	if (!Table)
	{
		UE_LOG(LogUAV, Warning, TEXT("AerodynamicProfileLookup: DataTable is null"));
		return nullptr;
	}

	// Ім'я рядка формується за конвенцією: "FLAP_{кут}_Deg" (наприклад "FLAP_0_Deg", "FLAP_-15_Deg")
	FName RowName = FName(*FString::Printf(TEXT("FLAP_%d_Deg"), FlapAngle));
	return Table->FindRow<FAerodynamicProfileRow>(RowName, TEXT("AerodynamicProfileLookup"));
}
