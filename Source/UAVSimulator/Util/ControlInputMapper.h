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
	 * Перетворює нормалізований сигнал керування [-1, 1] на кут відхилення закрилка в діапазоні [MinAngle, MaxAngle].
	 * При InputSignal == 0 результат дорівнює середньому значенню діапазону (нейтральне положення).
	 * Від'ємні значення відображаються до [Mid, MinAngle], додатні — до [Mid, MaxAngle].
	 * @param MinAngle    — мінімальний кут відхилення в градусах.
	 * @param MaxAngle    — максимальний кут відхилення в градусах.
	 * @param InputSignal — нормалізований сигнал [-1, 1].
	 * @return Кут відхилення закрилка в градусах.
	 */
	static float MapInputToFlapAngle(float MinAngle, float MaxAngle, float InputSignal);

	/**
	 * Визначає кут відхилення для поверхні залежно від її типу та ознаки дзеркальності.
	 * Елерон: лівий/правий канал залежно від bIsMirror.
	 * Руль висоти: лівий/правий канал залежно від bIsMirror.
	 * Руль напрямку: єдиний канал RudderAngle.
	 * Для невідомого типу повертає 0.
	 * @param FlapType    — тип органу керування (EFlapType).
	 * @param bIsMirror   — true для дзеркальної (правої) поверхні.
	 * @param MinAngle    — мінімальний кут відхилення в градусах.
	 * @param MaxAngle    — максимальний кут відхилення в градусах.
	 * @param ControlState — поточний стан органів керування.
	 * @return Кут відхилення закрилка в градусах.
	 */
	static float ResolveFlapAngle(EFlapType FlapType, bool bIsMirror, float MinAngle, float MaxAngle, FControlInputState ControlState);
};
