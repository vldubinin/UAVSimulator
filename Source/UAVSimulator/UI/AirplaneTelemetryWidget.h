#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AirplaneTelemetryWidget.generated.h"

class AAirplane;
class ACesiumGeoreference;
class UTextBlock;

/**
 * Легкий віджет для відображення телеметрії. Зберігає посилання на спостережуваний AAirplane
 * (встановлюється через SetAirplane, за тим самим патерном, що й UCameraViewWidget) і надає
 * BlueprintPure геттери для висоти/швидкості/тангажу/крену. Розкладка лишається в Widget
 * Blueprint (WBP_AirplaneTelemetry), а самі значення оновлює NativeTick: у Blueprint достатньо
 * додати TextBlock'и з іменами SpeedValueText / AltitudeValueText / ThrottleValueText /
 * ThrustValueText / PitchValueText / RollValueText (всі опційні) — форматування та одиниці виміру (км/год, м, °) робить C++.
 */
UCLASS()
class UAVSIMULATOR_API UAirplaneTelemetryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Telemetry")
	void SetAirplane(AAirplane* InAirplane);

	/**
	 * Висота ЛА (метри) — геодезична висота з ACesiumGeoreference (та сама, що в сенсорі
	 * drone_geo_position), тож коректна й далеко від початку координат Georeference, де сирий
	 * Z актора не враховує кривину Землі. Якщо Georeference нема — резерв: Z актора відносно
	 * початку світу.
	 */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetAltitudeMeters() const;

	/** Справжня повітряна швидкість ЛА (м/с) — швидкість фізичного тіла фюзеляжу, як у логах. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetAirspeedMs() const;

	/** Та сама швидкість у км/год (для звірки з крейсерською ~210). */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetAirspeedKmh() const;

	/** Положення дроселя (газу) двигуна, відсотки [0,100] — фактичне, після інерції розкручування. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetThrottlePercent() const;

	/** Фактична тяга двигуна, ньютони. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetThrustN() const;

	/** Кут тангажу (pitch) актора, градуси. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetPitchDeg() const;

	/** Кут крену (roll) актора, градуси. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetRollDeg() const;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	TObjectPtr<AAirplane> Airplane;

private:
	// Значення телеметрії — прив'язуються до однойменних TextBlock'ів (змінних) у
	// WBP_AirplaneTelemetry. Усі опційні: відсутній TextBlock просто не оновлюється.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpeedValueText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AltitudeValueText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ThrottleValueText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ThrustValueText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PitchValueText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RollValueText;

	/** Лінькаво знайдений Georeference (потрібен лише для висоти). */
	mutable TWeakObjectPtr<ACesiumGeoreference> CachedGeoreference;
};
