#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/Chord.h"

class USceneComponent;

/**
 * Helpers for converting local-space positions and chords to world space via a USceneComponent's transform.
 */
class UAVSIMULATOR_API CoordinateTransformUtil
{
public:
	/**
	 * Перетворює масив точок з локального простору компонента у світовий.
	 * @param Component      — компонент, чий трансформ використовується.
	 * @param LocalPositions — масив точок у локальному просторі (cm).
	 * @return Масив точок у світовому просторі (cm).
	 */
	static TArray<FVector> LocalToWorld(USceneComponent* Component, const TArray<FVector>& LocalPositions);

	/**
	 * Перетворює одну точку з локального простору компонента у світовий.
	 * @param Component     — компонент, чий трансформ використовується.
	 * @param LocalPosition — точка у локальному просторі (cm).
	 * @return Точка у світовому просторі (cm).
	 */
	static FVector         LocalToWorld(USceneComponent* Component, FVector LocalPosition);

	/**
	 * Перетворює обидві точки хорди з локального простору компонента у світовий.
	 * @param Component  — компонент, чий трансформ використовується.
	 * @param LocalChord — хорда у локальному просторі.
	 * @return Хорда у світовому просторі.
	 */
	static FChord          ChordLocalToWorld(USceneComponent* Component, FChord LocalChord);
};
