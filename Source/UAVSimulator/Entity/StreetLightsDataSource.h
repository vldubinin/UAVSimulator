#pragma once

#include "CoreMinimal.h"
#include "StreetLightsDataSource.generated.h"

/** Звідки AStreetLightsManager бере будівлі, біля яких розставляє вогні. */
UENUM(BlueprintType)
enum class EStreetLightsDataSource : uint8
{
	/** UCustomSurroundingsScannerComponent — фіксований список ObjectsJson із готовим footprint (прямокутник по периметру). */
	Custom UMETA(DisplayName = "Custom"),

	/** UCesiumSurroundingsScannerComponent — об'єкти з Cesium-метаданих у полі зору камери (один вогонь на об'єкт). */
	Cesium UMETA(DisplayName = "Cesium"),
};
