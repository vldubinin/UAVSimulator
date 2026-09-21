#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FogManager.generated.h"

class AExponentialHeightFog;

/**
 * Симуляція туману одним параметром. Незалежний від AAirplane актор рівня — той самий принцип,
 * що й ARainEffectManager/ATimeOfDayManager (лениво спавниться
 * UEnvironmentSectionWidget::GetFogManager(), якщо в рівні його нема).
 *
 * Керує AExponentialHeightFog: якщо такий актор уже є в рівні — бере його, інакше спавнить свій.
 * Туман — це штатний рушійний ExponentialHeightFog, тож він автоматично потрапляє і в основний
 * вид, і в USceneCaptureComponent2D бортової камери (без жодних правок матеріалів).
 *
 * `FogIntensity` [0,100] — єдине джерело правди: `0` = туман вимкнено (компонент невидимий,
 * окремого прапорця нема), `100` = максимальний (MaxFogDensity). Щільність росте квадратично
 * від інтенсивності, щоб нижня половина шкали давала легку димку, а не одразу стіну.
 */
UCLASS()
class UAVSIMULATOR_API AFogManager : public AActor
{
	GENERATED_BODY()

public:
	AFogManager();

	/** Інтенсивність туману [0,100]: 0 — вимкнено, 100 — максимум. */
	UFUNCTION(BlueprintCallable, Category = "Fog")
	void SetFogIntensity(float NewIntensity);

	UFUNCTION(BlueprintPure, Category = "Fog")
	float GetFogIntensity() const { return FogIntensity; }

	/** FogDensity (одиниці ExponentialHeightFog) при FogIntensity = 100. Орієнтир: 0.02 — дефолт
	 *  рушія (легка димка), ~0.5 — щільний туман з видимістю в кількадесят метрів. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0.0"))
	float MaxFogDensity = 0.5f;

	/** FogHeightFalloff туману. Мале значення розтягує туман по висоті, щоб він накривав і
	 *  БПЛА на сотнях метрів (дефолт рушія 0.2 — туман зникає вже за ~50 м над FogHeight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0.001"))
	float FogHeightFalloff = 0.02f;

	/** Дальність (см), до якої туман ще не діє — щоб він не заливав кабіну/носа літака. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0.0"))
	float FogStartDistance = 0.0f;

	/** Volumetric fog (розсіювання світла в тумані): гарніше, але дорожче і діє лише на ближній дистанції. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog")
	bool bUseVolumetricFog = false;

protected:
	virtual void BeginPlay() override;

private:
	/** Знаходить AExponentialHeightFog у рівні або спавнить новий (кешується). */
	AExponentialHeightFog* GetOrCreateFogActor();

	void ApplyFog();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "100.0"))
	float FogIntensity = 0.0f;

	TWeakObjectPtr<AExponentialHeightFog> CachedFogActor;
};
