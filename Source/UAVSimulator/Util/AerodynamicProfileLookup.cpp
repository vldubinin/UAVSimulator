#include "AerodynamicProfileLookup.h"
#include "UAVSimulator/UAVSimulator.h"

FAerodynamicProfileRow* AerodynamicProfileLookup::FindProfile(UDataTable* Table, int32 FlapAngle)
{
	if (!Table)
	{
		UE_LOG(LogUAV, Warning, TEXT("AerodynamicProfileLookup: DataTable is null"));
		return nullptr;
	}

	FName RowName = FName(*FString::Printf(TEXT("FLAP_%d_Deg"), FlapAngle));
	return Table->FindRow<FAerodynamicProfileRow>(RowName, TEXT("AerodynamicProfileLookup"));
}
