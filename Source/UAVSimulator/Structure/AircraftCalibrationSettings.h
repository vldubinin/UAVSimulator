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

	/**
	 * Очікуваний центр мас прототипу (локальні координати, см). Поки не використовується —
	 * заготовка під майбутнє калібрування CenterOfMass разом із розмахом крил.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Калібрування літака",
		meta = (DisplayName = "Очікуваний центр мас (не використовується)"))
	FVector ExpectedCenterOfMass = FVector::ZeroVector;
};
