#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimeOfDayManager.generated.h"

class ACesiumSunSky;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSolarTimeAdvanced, double, NewSolarTime);

/**
 * Автоматично рухає час доби: щотіку додає до ACesiumSunSky::SolarTime (години, [0,24))
 * DeltaTime * TimeSpeed / 3600 і перераховує сонце через UpdateSun(). Незалежний від
 * AAirplane актор рівня — той самий принцип, що й ARainEffectManager/AStreetLightsManager
 * (лениво спавниться UEnvironmentSectionWidget::GetTimeOfDayManager(), якщо в рівні його нема).
 *
 * Дату (Year/Month/Day сонця) не чіпає — при переході через 24:00 доба просто починається
 * заново. Ручне введення SolarTime у меню працює як і раніше: менеджер додає свій приріст
 * поверх того, що зараз лежить у SunSky->SolarTime.
 *
 * Після кожного UpdateSun() розсилає OnSolarTimeAdvanced — меню оновлює спінбокс часу та
 * перераховує місяць/зорі (UEnvironmentSectionWidget::UpdateNightVisuals), бо сам SunSky
 * їх не рухає (див. Docs/13-Environment-Actors.md).
 */
UCLASS()
class UAVSIMULATOR_API ATimeOfDayManager : public AActor
{
	GENERATED_BODY()

public:
	ATimeOfDayManager();

	/** Швидкість ходу часу — у разів швидше за реальний: `1` = реальний час, `60` = симуляційна
	 *  хвилина за секунду (доба за 24 хв), `0` = час стоїть (єдиний вимикач, окремого
	 *  прапорця нема — той самий принцип, що RainIntensity). */
	UFUNCTION(BlueprintCallable, Category = "TimeOfDay")
	void SetTimeSpeed(float NewTimeSpeed);

	UFUNCTION(BlueprintPure, Category = "TimeOfDay")
	float GetTimeSpeed() const { return TimeSpeed; }

	/** Як часто (сек) перераховувати сонце. Час накопичується щотіку, а UpdateSun()
	 *  (яке чіпає SkyLight/атмосферу) викликається не частіше за цей інтервал. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeOfDay", meta = (ClampMin = "0.0"))
	float UpdateInterval = 0.1f;

	/** Розсилається після кожного застосованого приросту часу; параметр — нове SolarTime (год). */
	UPROPERTY(BlueprintAssignable, Category = "TimeOfDay")
	FOnSolarTimeAdvanced OnSolarTimeAdvanced;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	ACesiumSunSky* GetSunSky();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeOfDay", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "100000.0"))
	float TimeSpeed = 60.0f;

	TWeakObjectPtr<ACesiumSunSky> CachedSunSky;

	/** Накопичений, ще не застосований приріст у годинах. */
	double PendingHours = 0.0;
	float TimeSinceLastUpdate = 0.0f;
};
