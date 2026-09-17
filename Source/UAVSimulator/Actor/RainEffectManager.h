#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RainEffectManager.generated.h"

class AAirplane;
class UNiagaraSystem;
class UNiagaraComponent;

/**
 * Спавнить дощовий Niagara-ефект (RainSystem) над кожним AAirplane у світі — незалежно
 * від того, коли й скільки їх заспавнили/знищили (StartSimulation/StopSimulation,
 * RecordTarget/PlaybackAndTrack тощо). AAirplane.h/.cpp і його Blueprint нічого не знають
 * про Niagara — увесь зв'язок односторонній, ззовні, через цей менеджер (той самий принцип
 * незалежності, що й AEnvironmentActorManager/AWindActor щодо UFlightDynamicsComponent —
 * див. Docs/13-Environment-Actors.md).
 *
 * Щотіку оновлює позицію вже відстежуваних ефектів (щоб дощ не відставав від літака), і раз
 * на RescanInterval секунд пере-скановує світ через GetAllActorsOfClass — додає записи для
 * нових AAirplane і прибирає (разом зі спавненим UNiagaraComponent) ті, чий літак знищено.
 *
 * Розміщується вручну в рівні (як AEnvironmentActorManager) — один менеджер на рівень.
 */
UCLASS()
class UAVSIMULATOR_API ARainEffectManager : public AActor
{
	GENERATED_BODY()

public:
	ARainEffectManager();

	/** Niagara-система дощу (NS_Rain), що спавниться над кожним літаком. Поки не задана —
	 *  менеджер нічого не робить (RescanAirplanes() виходить одразу, як і Refresh* в
	 *  AEnvironmentActorManager без відповідного *ActorClass). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain")
	TObjectPtr<UNiagaraSystem> RainSystem;

	/** Зміщення дощу відносно GetActorLocation() літака по всіх осях, см (X/Y — щоб
	 *  дощ падав, наприклад, з випередженням/збоку від напрямку польоту; Z — висота
	 *  над літаком). Додається як є, без урахування орієнтації літака (світові осі),
	 *  тож однакове для будь-якого курсу/крену — узгоджено з тим, що сам дощ ніколи не
	 *  обертається (див. коментар UpdateRainPositions()). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain")
	FVector RainOffset = FVector(0.0f, 0.0f, 2000.0f);

	/** Як часто (сек) пере-сканувати світ на нові/знищені AAirplane. Позиція вже
	 *  відстежуваних ефектів оновлюється щотіку незалежно від цього інтервалу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain")
	float RescanInterval = 1.0f;

	/** Множник інтенсивності дощу — **і** глобальний вимикач в одному полі: `0` — дощу
	 *  нема взагалі (усі активні ефекти знищуються, спавн нових зупиняється), `1` —
	 *  базова інтенсивність ассету, `>1` — сильніше за базову. Ненульове значення
	 *  прокидається в кожен активний UNiagaraComponent як User-параметр float з іменем
	 *  IntensityParameterName (див. .cpp) — NS_Rain має експонувати User Parameter з
	 *  такою самою назвою й використовувати його (типово — множником на Spawn Rate),
	 *  інакше зміна значення ні на що не вплине. */
	UFUNCTION(BlueprintCallable, Category = "Rain")
	void SetRainIntensity(float NewIntensity);

	UFUNCTION(BlueprintPure, Category = "Rain")
	float GetRainIntensity() const { return RainIntensity; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Знищує записи, чий AAirplane уже невалідний (TWeakObjectPtr stale), і додає нові —
	 *  по одному UNiagaraComponent на кожен AAirplane, знайдений через GetAllActorsOfClass,
	 *  якого ще нема в ActiveRainEffects. */
	void RescanAirplanes();

	/** Виставляє світову позицію кожного відстежуваного UNiagaraComponent над поточним
	 *  GetActorLocation() свого AAirplane, з нульовою ротацією — дощ завжди зверху й завжди
	 *  падає прямо вниз, незалежно від крену/тангажу літака. */
	void UpdateRainPositions() const;

	UNiagaraComponent* SpawnRainForAirplane(const AAirplane* Airplane) const;

	/** Знищує всі UNiagaraComponent з ActiveRainEffects і чистить мапу — спільна логіка
	 *  для EndPlay() і SetRainIntensity(0). */
	void DestroyAllRainEffects();

	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<AAirplane>, TObjectPtr<UNiagaraComponent>> ActiveRainEffects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "5.0"))
	float RainIntensity = 1.0f;

	void ApplyIntensity(UNiagaraComponent* RainComp) const;

	float TimeSinceLastRescan = 0.0f;
};
