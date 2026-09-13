// Заповніть примітку про авторські права на сторінці Description у Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/Chord.h"
#include "AirfoilData.h"

/**
 * Утиліти геометрії профілю крила без внутрішнього стану: пошук хорди, масштабування профілю,
 * нормалізація та переведення в 3D. Перетворення координатних просторів знаходяться в CoordinateTransformUtil.
 */
class UAVSIMULATOR_API AerodynamicUtil
{
public:
	/**
	 * Знаходить хорду 2D-набору точок профілю як відрізок між точкою з мінімальним X та максимальним X.
	 * @param Points — масив 2D-точок профілю крила.
	 * @param Offset — зміщення, яке додається до обох точок хорди (за замовчуванням нуль).
	 * @return FChord з точками мінімального і максимального X.
	 */
	static FChord FindChord(TArray<FAirfoilPointData> Points, FVector Offset = FVector::ZeroVector);

	/**
	 * Знаходить хорду 3D-масиву точок за екстремумами по осі X.
	 * StartPoint відповідає максимальному X (передня кромка), EndPoint — мінімальному X.
	 * @param Points — масив 3D-точок.
	 * @return FChord (MaxX → MinX).
	 */
	static FChord FindChord(TArray<FVector> Points);

	/**
	 * Масштабує 2D-профіль відносно середини його хорди.
	 * @param Points      — масив 2D-точок профілю.
	 * @param ScaleFactor — коефіцієнт масштабування.
	 * @return Масштабований масив точок.
	 */
	static TArray<FAirfoilPointData> Scale(TArray<FAirfoilPointData> Points, float ScaleFactor);

	/**
	 * Інвертує вісь X усіх точок профілю (нормалізація конвенції координат).
	 * @param Points — масив 2D-точок профілю.
	 * @return Нормалізований масив (X = -X).
	 */
	static TArray<FAirfoilPointData> NormalizePoints(TArray<FAirfoilPointData> Points);

	/**
	 * Масштабує 2D-профіль до заданої довжини хорди та переводить у 3D з зміщенням.
	 * @param Profile             — нормалізований 2D-профіль.
	 * @param ChordLength         — поточна довжина хорди профілю.
	 * @param ExpectedChordLength — цільова довжина хорди після масштабування.
	 * @param Offset              — зміщення у 3D-просторі для розміщення секції.
	 * @return Масив 3D-точок.
	 */
	static TArray<FVector> ConvertTo3DPoints(TArray<FAirfoilPointData> Profile, float ChordLength, float ExpectedChordLength, FVector Offset);

	/**
	 * Переводить 2D-точки профілю у 3D, встановлюючи Y = 0 і додаючи зміщення.
	 * @param Points — масив 2D-точок.
	 * @param Offset — зміщення у 3D-просторі.
	 * @return Масив 3D-точок FVector(X, 0, Z) + Offset.
	 */
	static TArray<FVector> AdaptTo(TArray<FAirfoilPointData> Points, FVector Offset);
};
