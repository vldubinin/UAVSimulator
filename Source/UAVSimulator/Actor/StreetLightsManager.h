#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/Structure/StreetLightBuilding.h"
#include "StreetLightsManager.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UCustomSurroundingsScannerComponent;

/**
 * Симулює вогні нічних вулиць на Cesium-поверхні. Виявлення НЕ реалізує власний sweep і не
 * читає Cesium feature-метадані напряму — натомість напряму споживає результати вже
 * перевіреного робочого UCustomSurroundingsScannerComponent (додається лениво на кожен
 * AAirplane, якщо його там ще нема): цей компонент бере об'єкти зі свого власного ObjectsJson
 * (фіксований список — id/type/bbox чотирьох географічних кутів), сам прив'язує кожен кут до
 * поверхні тайла Cesium (ResolveGroundHeights) і повертає LatestScanResults — по одному
 * FCustomSurroundingObject на кожен наразі видимий об'єкт, УЖЕ з готовим footprint
 * (BBoxCornersWorldMeters, чотири кути, прив'язані до рельєфу).
 *
 * ВАЖЛИВО: ObjectsJson цього сканера — не автоматичне джерело "усі будівлі поруч", а фіксований,
 * заздалегідь підготовлений список (дефолт — заглушка з двох прикладних будівель у
 * UCustomSurroundingsScannerComponent.cpp). Щоб вогні з'являлися біля реальних будівель на
 * маршруті літака, ObjectsJson щойно заспавненого сканера (GetOrCreateScannerFor) потрібно
 * заповнити вручну (напр. через редактор компонента на Blueprint літака) реальними id/bbox —
 * цей менеджер сам нічого туди не генерує.
 *
 * Історія: перші версії цього класу самі обчислювали "де має бути" об'єкт (з lat/long Cesium-
 * метаданих, потім — з власної сітки вертикальних трейсів навколо XY літака, потім — через
 * UCesiumSurroundingsScannerComponent) — кожна мала свої проблеми (кривина Землі, LOD-неточність
 * метаданих, відсутність готового footprint). UCustomSurroundingsScannerComponent на відміну
 * від них одразу дає прив'язаний до рельєфу footprint без потреби в окремому ground-trace тут.
 *
 * Кожен об'єкт зі сканера (з bGroundHeightResolved) зливається за своїм ObjectID у
 * TrackedBuildingsMap; footprint = BBoxCornersWorldMeters (реальні чотири кути), вогні
 * розставляються вздовж периметра з кроком LightSpacingMeters, висота — з уже прив'язаного до
 * рельєфу кута footprint + LightHeightMeters (жодного додаткового ground-trace тут не потрібно).
 *
 * Видалення — MaxTrackingDistanceMeters від будь-якого поточного AAirplane: коли літак
 * відлітає, будівля перестає бути актуальною незалежно від внутрішнього стану сканера.
 *
 * Рендер — один постійний UNiagaraComponent з масивом позицій вогнів
 * (UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector, той самий підхід, що вже
 * working у UAeroVisualizerComponent::UpdateNiagaraWakeData для WakePositions) — масштабується
 * на тисячі вогнів без per-light акторів/компонентів. Яскравість — єдина публічна точка
 * керування, SetBrightness()/GetBrightness() у [0,100]: 0 деактивує ефект (вимкнено), >0
 * активує і прокидає User Parameter "Brightness" (Float, [0,1]) — NS-ассет (StreetLightsSystem)
 * має його експонувати й використовувати (типово — множником на розмір/яскравість спрайта),
 * інакше зміна значення ні на що не вплине, той самий принцип, що "Intensity" у ARainEffectManager.
 *
 * Розміщується вручну в рівні (як AEnvironmentActorManager/ARainEffectManager) — один менеджер
 * на рівень; StreetLightsSystem призначається вручну (поки не задано — нічого не робить, той
 * самий принцип, що RainSystem без відповідного ассету).
 */
UCLASS()
class UAVSIMULATOR_API AStreetLightsManager : public AActor
{
	GENERATED_BODY()

public:
	AStreetLightsManager();

	/**
	 * Niagara-система вогнів (напр. NS_StreetLights), що споживає масив User.LightPositions
	 * (Vector3 Array, GetVectorArrayCount/SelectVectorFromArray) і User Parameter Brightness
	 * (Float, [0,1], домножується на колір спрайта). Поки не задана — менеджер нічого не
	 * спавнить (той самий принцип, що RainSystem у ARainEffectManager).
	 *
	 * NS_StreetLights очікує спалахування частинок безперервним SpawnRate (не разовим
	 * burst) з фіксованим Lifetime — це необхідно, бо Niagara Custom Hlsl-вузли не можуть
	 * викликати функції Data Interface масивів (перевірено емпірично: `.Get()`/`.Length()`
	 * на User-параметрі масиву валить компіляцію з "GetDuplicatedDataInterfaceCDOForClass
	 * failed"), тож позиція кожної частинки береться через штатний dynamic input
	 * SelectVectorFromArray (ВИПАДКОВИЙ вибір елемента масиву, а не точний ExecIndex) —
	 * періодичне респавнення (Lifetime = NiagaraParticleLifetimeSeconds) дає статистично
	 * прийнятне покриття всіх позицій із часом, ціною відсутності гарантії "рівно один вогонь
	 * на будівлю в кожен момент". SpawnRate обчислюється тут (кількість вогнів /
	 * NiagaraParticleLifetimeSeconds), а не в Niagara, бо User-параметри — read-only в
	 * графі, і навіть проста арифметика над довжиною масиву там теж недоступна.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights")
	TObjectPtr<UNiagaraSystem> StreetLightsSystem;

	/** Час життя (сек) однієї частинки в NS_StreetLights (InitializeParticle::Lifetime) —
	 *  має збігатися зі значенням, заданим у самому Niagara-ассеті; звідси рахується
	 *  User.TargetSpawnRate = кількість вогнів / це значення. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights", meta = (ClampMin = 0.1f))
	float NiagaraParticleLifetimeSeconds = 20.0f;

	// ── Сканер (лениво додається на кожен AAirplane, якщо його там ще нема) ─────────────

	/** Радіус виявлення сканера, у метрах (UCustomSurroundingsScannerComponent::ScanRadiusMeters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Scan", meta = (ClampMin = 1.0f))
	float ScannerScanRadiusMeters = 2000.0f;

	/** Канал колізії, яким сканер прив'язує кути bbox до поверхні тайла (UCustomSurroundingsScannerComponent::GroundTraceChannel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Scan")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

	/**
	 * Максимальна відстань (м) від будь-якого поточного AAirplane, на якій будівля ще
	 * вважається "актуальною". Коли літак відлітає далі — вогні там прибираються, незалежно
	 * від того, чи сканер саме зараз ще бачить об'єкт.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Scan", meta = (ClampMin = 1.0f))
	float MaxTrackingDistanceMeters = 2500.0f;

	// ── Розміщення вогнів на основі footprint (BBoxCornersWorldMeters) ──────────────────

	/** Крок між вогнями вздовж периметра footprint, у метрах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Footprint", meta = (ClampMin = 1.0f))
	float LightSpacingMeters = 18.0f;

	/** Висота "стовпа" над знайденою поверхнею землі біля будівлі, у метрах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Footprint", meta = (ClampMin = 0.0f))
	float LightHeightMeters = 4.0f;

	// ── Дебаг ──────────────────────────────────────────────────────────────────────────

	/** Малює footprint і позиції вогнів кожної відстежуваної будівлі. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Debug")
	bool bDrawDebugFootprints = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Debug")
	FColor FootprintDebugColor = FColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Debug")
	FColor LightDebugColor = FColor::Orange;

	/** Дзеркало TrackedBuildingsMap — кожна наразі відстежувана будівля, для інспекції в редакторі. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Street Lights|Debug")
	TArray<FStreetLightBuilding> TrackedBuildings;

	/** Яскравість [0,100] — єдина точка керування: 0 вимикає ефект повністю, 100 — максимум.
	 *  Керується SpinBoxStreetLightsBrightness у UEnvironmentSectionWidget. */
	UFUNCTION(BlueprintCallable, Category = "Street Lights")
	void SetBrightness(float NewBrightness);

	UFUNCTION(BlueprintPure, Category = "Street Lights")
	float GetBrightness() const { return Brightness; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 * Один прохід: для кожного AAirplane у світі — GetOrCreateScannerFor() (лениво додає й
	 * реєструє UCustomSurroundingsScannerComponent, якщо його там ще нема), потім читає його
	 * LatestScanResults. Кожен об'єкт із bGroundHeightResolved і ще не відстежуваний —
	 * BuildLightsForObject() + AddBuilding(). Наприкінці — RunValiditySweep() за позиціями
	 * літаків цього проходу. Викликається щотіку (сама робота дешева — лише читання вже
	 * обчисленого масиву; фактичне сканування виконує сам UCustomSurroundingsScannerComponent).
	 */
	void Scan();

	/**
	 * Повертає сканер для Airplane — кешований у OwnScannersByAirplane (лише щоб не викликати
	 * FindComponentByClass щотіку), а по суті: уже наявний UCustomSurroundingsScannerComponent
	 * на літаку (типово — заздалегідь розміщений на Blueprint для реальної сенсорної шини),
	 * якщо він є, інакше — новий, лениво доданий і зареєстрований із застосованими Scanner*-
	 * налаштуваннями. Навмисно ВИКОРИСТОВУЄ вже наявний сканер (а не створює власний
	 * ізольований), щоб вогні йшли з тих самих даних/налаштувань (ObjectsJson тощо), які видно
	 * на екрані через debug-промені цього сканера.
	 */
	UCustomSurroundingsScannerComponent* GetOrCreateScannerFor(AActor* Airplane);

	/**
	 * З уже готового footprint об'єкта сканера (Corners, чотири кути, прив'язані до рельєфу,
	 * у світових метрах) розставляє вогні вздовж периметра з кроком LightSpacingMeters — висота
	 * кожного бере Z відповідної точки на периметрі (інтерпольований між кутами, вже на рівні
	 * рельєфу) + LightHeightMeters. Жодного додаткового ground-trace не потрібно.
	 */
	bool BuildLightsForObject(const FString& Key, const TArray<FVector>& Corners, FStreetLightBuilding& OutBuilding) const;

	/**
	 * Прибирає з TrackedBuildingsMap будівлі, що зараз далі за MaxTrackingDistanceMeters від
	 * УСІХ AirplaneLocationsCm — коли літак відлітає, вогні там більше не актуальні незалежно
	 * від внутрішнього стану сканера.
	 */
	void RunValiditySweep(const TArray<FVector>& AirplaneLocationsCm);

	/** Перебудовує плаский масив позицій вогнів з TrackedBuildingsMap і штовхає його в NiagaraComp. */
	void RebuildNiagaraArrays();

	void AddBuilding(const FString& Key, const FStreetLightBuilding& Building);
	void RemoveBuilding(const FString& Key);

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> NiagaraComp;

	/** Власні (лише цього менеджера) сканери, по одному на AAirplane — див. GetOrCreateScannerFor(). */
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UCustomSurroundingsScannerComponent>> OwnScannersByAirplane;

	/** Постійне сховище наразі відстежуваних будівель, ключоване ObjectID сканера. Змінюється
	 *  лише через AddBuilding()/RemoveBuilding() — той самий ObjectStorage-патерн, що в
	 *  UCesiumSurroundingsScannerComponent/UCustomSurroundingsScannerComponent. TrackedBuildings
	 *  дзеркалить його для інспекції. */
	TMap<FString, FStreetLightBuilding> TrackedBuildingsMap;

	/** true, якщо TrackedBuildingsMap змінився з моменту останнього RebuildNiagaraArrays(). */
	bool bLightPositionsDirty = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "100.0"))
	float Brightness = 0.0f;
};
