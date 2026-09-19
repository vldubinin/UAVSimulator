#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "StreetLightBuilding.generated.h"

/**
 * Одна будівля, виявлена AStreetLightsManager через ICesium3DTilesetLifecycleEventReceiver::
 * OnTileMeshPrimitiveLoaded, з наближеним контуром (footprint), отриманим фізичним
 * семплінгом дахуа (не з метаданих — Cesium-тайлсет дає лише опорну точку lat/long +
 * висоту на будівлю, без форми), і позиціями вогнів, розставлених уздовж цього контуру.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FStreetLightBuilding
{
	GENERATED_BODY()

	/** Стабільний ключ будівлі — ім'я примітива тайла + FeatureID у ньому. */
	UPROPERTY(BlueprintReadOnly, Category = "Street Lights")
	FString ObjectID;

	/**
	 * Примітив тайла, з якого цю будівлю виявлено — джерело правди для перевірки "тайл ще
	 * завантажений?" (IsValid() && IsRegistered()); Cesium знищує примітиви вивантажених
	 * тайлів, тож невалідний вказівник тут однозначно означає "тайл вивантажено".
	 */
	UPROPERTY()
	TWeakObjectPtr<UPrimitiveComponent> SourcePrimitive;

	/** Наближений bounding-прямокутник даху будівлі (світові метри), отриманий кільцевим семплінгом. */
	UPROPERTY(BlueprintReadOnly, Category = "Street Lights")
	TArray<FVector> FootprintCornersWorldMeters;

	/** Позиції вогнів уздовж периметра footprint (світові метри, на висоті "стовпа"). */
	UPROPERTY(BlueprintReadOnly, Category = "Street Lights")
	TArray<FVector> LightPositionsWorldMeters;
};
