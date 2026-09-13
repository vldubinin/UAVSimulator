#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/Chord.h"

class USceneComponent;

/**
 * Допоміжні функції для перетворення точок і хорд з локального простору у світовий через трансформ USceneComponent.
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
