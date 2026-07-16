#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AirplaneTelemetryWidget.generated.h"

class AAirplane;

/**
 * Thin telemetry readout widget. Holds a reference to the observed AAirplane (set via
 * SetAirplane, same pattern as UCameraViewWidget) and exposes BlueprintPure getters for
 * altitude/speed/pitch/roll. Create a Widget Blueprint with this as its parent class, add
 * TextBlocks, and bind their Text property to these getters (wrap with a Format Text node
 * for units/decimals) — the actual layout stays in the Blueprint, not in native code.
 */
UCLASS()
class UAVSIMULATOR_API UAirplaneTelemetryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Telemetry")
	void SetAirplane(AAirplane* InAirplane);

	/** Висота ЛА над рівнем світу (метри), Z світової позиції актора. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetAltitudeMeters() const;

	/** Швидкість руху ЛА (м/с), довжина вектора швидкості актора. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetAirspeedMs() const;

	/** Кут тангажу (pitch) актора, градуси. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetPitchDeg() const;

	/** Кут крену (roll) актора, градуси. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetRollDeg() const;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	TObjectPtr<AAirplane> Airplane;
};
