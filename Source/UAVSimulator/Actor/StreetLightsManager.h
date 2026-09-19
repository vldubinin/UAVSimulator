#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/Structure/StreetLightBuilding.h"
#include "UAVSimulator/Entity/StreetLightsDataSource.h"
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
	 * (Vector3 Array, SelectVectorFromArray) і User Parameter Brightness (Float, [0,1],
	 * домножується на колір спрайта). Поки не задана — менеджер нічого не спавнить (той самий
	 * принцип, що RainSystem у ARainEffectManager).
	 *
	 * NS_StreetLights спавнить частинки РАЗОВИМ burst (SpawnBurst_Instantaneous, Spawn Count =
	 * User.TargetSpawnCount) із величезним Lifetime частинки (у самому ассеті) — не
	 * безперервним SpawnRate: RebuildNiagaraArrays() викликає NiagaraComp->Activate(true)
	 * щоразу, коли масив вогнів змінюється, примусово перезапускаючи симуляцію (новий burst на
	 * актуальний TargetSpawnCount) — так увесь масив з'являється одразу, а не поступово
	 * заповнюється, і частинки не гинуть і не "перетасовуються" самі по собі між цими
	 * перезапусками. Позиція кожної частинки береться через штатний dynamic input
	 * SelectVectorFromArray (ВИПАДКОВИЙ вибір елемента масиву, а не точний ExecIndex — Niagara
	 * Custom Hlsl-вузли не можуть викликати функції Data Interface масивів, перевірено
	 * емпірично: `.Get()`/`.Length()` на User-параметрі масиву валить компіляцію з
	 * "GetDuplicatedDataInterfaceCDOForClass failed"), тож немає гарантії "рівно один вогонь на
	 * будівлю", лише статистично прийнятне покриття.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights")
	TObjectPtr<UNiagaraSystem> StreetLightsSystem;

	// ── Сканер (лениво додається на кожен AAirplane, якщо його там ще нема) ─────────────

	/** Радіус виявлення сканера, у метрах (UCustomSurroundingsScannerComponent::ScanRadiusMeters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Scan", meta = (ClampMin = 1.0f))
	float ScannerScanRadiusMeters = 2000.0f;

	/** Канал колізії, яким сканер прив'язує кути bbox до поверхні тайла (UCustomSurroundingsScannerComponent::GroundTraceChannel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Scan")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

	/**
	 * Максимальна відстань (м) від будь-якого поточного AAirplane, на якій будівля ще
	 * вважається "актуальною". Коли літак відлітає далі — вогні там прибираються.
	 * 0 (типово) = ніколи не прибирати: вогні, що вже з'явились, лишаються назавжди
	 * (кожне прибирання/додавання перезапускає Niagara-систему й вогні на мить зникають).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Scan", meta = (ClampMin = 0.0f))
	float MaxTrackingDistanceMeters = 0.0f;

	/**
	 * Мінімальний інтервал (с) між перебудовами масиву Niagara (кожна перебудова робить
	 * Activate(true) — коротке "моргання"). Нові будівлі накопичуються й додаються одним пакетом.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Scan", meta = (ClampMin = 0.0f))
	float MinRebuildIntervalSeconds = 2.0f;

	// ── Розміщення вогнів на основі footprint (BBoxCornersWorldMeters) ──────────────────

	/** Крок між вогнями вздовж периметра footprint, у метрах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Footprint", meta = (ClampMin = 1.0f))
	float LightSpacingMeters = 18.0f;

	/** Висота "стовпа" над знайденою поверхнею землі біля будівлі, у метрах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Footprint", meta = (ClampMin = 0.0f))
	float LightHeightMeters = 4.0f;

	// ── Джерело Cesium: випадковий прямокутник навколо точки влучання ───────────────────

	/** Мінімальний розмір сторони випадкового прямокутника (м) навколо точки влучання Cesium-об'єкта. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Footprint", meta = (ClampMin = 1.0f))
	float CesiumFootprintMinSizeMeters = 15.0f;

	/** Максимальний розмір сторони випадкового прямокутника (м) навколо точки влучання Cesium-об'єкта. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights|Footprint", meta = (ClampMin = 1.0f))
	float CesiumFootprintMaxSizeMeters = 40.0f;

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

	/**
	 * Обирає джерело будівель: UCustomSurroundingsScannerComponent (footprint-прямокутник,
	 * вогні по периметру) або UCesiumSurroundingsScannerComponent (об'єкти Cesium-метаданих у
	 * полі зору камери — один вогонь на об'єкт у точці влучання, бо метадані не дають контуру).
	 * При зміні всі вже відстежувані вогні скидаються — обидва джерела мають різні ObjectID і
	 * геометрію, змішувати їх не можна. Керується ComboBoxStreetLightsDataSource у
	 * UEnvironmentSectionWidget.
	 */
	UFUNCTION(BlueprintCallable, Category = "Street Lights")
	void SetDataSource(EStreetLightsDataSource NewDataSource);

	UFUNCTION(BlueprintPure, Category = "Street Lights")
	EStreetLightsDataSource GetDataSource() const { return DataSource; }

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
	/**
	 * Джерело Cesium дає лише точку (влучання), без контуру. Будує навколо неї прямокутник із
	 * випадковими шириною/глибиною в [CesiumFootprintMinSizeMeters, CesiumFootprintMaxSizeMeters]
	 * і випадковим поворотом у горизонтальній площині. Генератор засіяний хешем Key, тож той самий
	 * об'єкт завжди дає той самий прямокутник. Повертає чотири кути (метри) у порядку обходу.
	 */
	TArray<FVector> MakeRandomFootprintAround(const FString& Key, const FVector& CenterMeters) const;

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

	/** Час (World->GetTimeSeconds()) останньої RebuildNiagaraArrays() — для MinRebuildIntervalSeconds. */
	double LastRebuildTimeSeconds = -1.0e9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "100.0"))
	float Brightness = 0.0f;

	/** Джерело будівель — змінюється через SetDataSource() (скидає вже відстежувані вогні). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Street Lights", meta = (AllowPrivateAccess = "true"))
	EStreetLightsDataSource DataSource = EStreetLightsDataSource::Custom;
};
