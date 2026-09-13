#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AirplaneTelemetryWidget.generated.h"

class AAirplane;

/**
 * Легкий віджет для відображення телеметрії. Зберігає посилання на спостережуваний AAirplane
 * (встановлюється через SetAirplane, за тим самим патерном, що й UCameraViewWidget) і надає
 * BlueprintPure геттери для висоти/швидкості/тангажу/крену. Створіть Widget Blueprint із цим
 * класом як батьківським, додайте TextBlock'и й прив'яжіть їхню властивість Text до цих геттерів
 * (обгорнувши вузлом Format Text для одиниць виміру/десяткових знаків) — сама розкладка
 * лишається в Blueprint, а не в нативному коді.
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

	/** Справжня повітряна швидкість ЛА (м/с) — швидкість фізичного тіла фюзеляжу, як у логах. */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetAirspeedMs() const;

	/** Та сама швидкість у км/год (для звірки з крейсерською ~210). */
	UFUNCTION(BlueprintPure, Category = "Telemetry")
	float GetAirspeedKmh() const;

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
