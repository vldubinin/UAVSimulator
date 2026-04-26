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
	 * Знаходить рядок полярного профілю для заданого кута відхилення закрилка.
	 * Ім'я рядка формується як "FLAP_{FlapAngle}_Deg" (наприклад, "FLAP_0_Deg").
	 * @param Table     — DataTable з полярними характеристиками профілю; nullptr — повертає nullptr.
	 * @param FlapAngle — кут відхилення закрилка в градусах (ціле число).
	 * @return Вказівник на FAerodynamicProfileRow або nullptr якщо таблиця або рядок відсутні.
	 */
	static FAerodynamicProfileRow* FindProfile(UDataTable* Table, int32 FlapAngle);
};
