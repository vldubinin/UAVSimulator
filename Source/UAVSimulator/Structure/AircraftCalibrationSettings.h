#pragma once

#include "CoreMinimal.h"
#include "AircraftCalibrationSettings.generated.h"

/**
 * Калібрування геометрії літака відносно реального прототипу. Задається на AAirplane,
 * застосовується один раз при спавні (AAirplane::BeginPlay).
 */
USTRUCT(BlueprintType)
struct FAircraftCalibrationSettings
{
	GENERATED_BODY()

	/**
	 * Реальний розмах крил прототипу, м. <= 0 — не масштабувати актора при спавні.
	 * Актор масштабується рівномірно так, щоб "дизайнерський" розмах крил
	 * (UFlightDynamicsComponent::GetDesignWingSpanCm) відповідав цьому значенню.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Калібрування літака",
		meta = (DisplayName = "Очікуваний розмах крил (м)", ClampMin = "0.0"))
	float ExpectedWingSpanMeters = 0.0f;

	/** Чи ставити центр мас у ExpectedCenterOfMass при спавні (замість ручного COMNudge на меші). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Калібрування літака",
		meta = (DisplayName = "Задати центр мас явно"))
	bool bApplyExpectedCenterOfMass = false;

	/**
	 * Очікуваний центр мас, см, у "дизайнерських" локальних координатах актора (ДО масштабування
	 * ExpectedWingSpanMeters — ті самі одиниці, що й розташування поверхонь у Blueprint).
	 * AAirplane::BeginPlay обчислює COMNudge меша так, щоб фізичний CoM потрапив саме сюди.
	 * Орієнтир: 25–30 % САХ крила позаду передньої кромки.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Калібрування літака",
		meta = (DisplayName = "Очікуваний центр мас (см, локально)", EditCondition = "bApplyExpectedCenterOfMass"))
	FVector ExpectedCenterOfMass = FVector::ZeroVector;
};
