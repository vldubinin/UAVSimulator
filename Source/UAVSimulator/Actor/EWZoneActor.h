// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EWZoneActor.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class ACesiumGeoreference;

/**
 * Наочний маркер зони дії РЕБ (Electronic Warfare) у персистентному рівні: прозора сфера
 * без колізії, що показує розташування й радіус дії глушіння. Розміщується вручну в
 * рівні — так само, як ACesiumGeoreference/ACesiumSunSky.
 *
 * Сама зона рахує, наскільки сильно вона глушить задану світову точку —
 * GetInterferenceIntensity() — щоб цю формулу не дублювали й не тримали в синку з Radius/
 * позицією консюменти на кшталт UUAVCameraComponent::UpdateEWInterference() (яка лише бере
 * максимум інтенсивності серед усіх зон, повернутих AUAVSimulatorGameModeBase::UpdateEWSettings()
 * через UUAVSimulationSubsystem::EWZones).
 *
 * Longitude/Latitude/Height зберігаються як єдине джерело правди (StoredLongitude/
 * StoredLatitude/StoredHeight, градуси/метри) — а НЕ вираховуються щоразу назад із
 * поточної Unreal-позиції актора. Позиція рахується наперед одним атомарним викликом
 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal з ПОВНОГО LLH-набору
 * й застосовується через SetActorLocation(). Це принципово: SpinBoxEWLocationX/Y комітяться
 * як дві окремі UI-події (спершу Longitude, потім Latitude), і якщо після зміни лише
 * Longitude реконвертувати назад через TransformUnrealPositionToLongitudeLatitudeHeight
 * (як робилося раніше), актор на секунду опиняється дуже далеко від точки дотику
 * тангенціальної площини Cesium — і зворотне перетворення звідти вже дає сміття
 * (сотні мільйонів см). Зберігання LLH окремо від Unreal-позиції усуває цей клас багів.
 * GetActorLocation() лишається справжньою Unreal world-позицією, яку й використовує
 * GetInterferenceIntensity() для рахунку відстані до довільної точки (напр. літака).
 *
 * Приховується з кожного онбордового SceneCaptureComponent2D через
 * UUAVCameraComponent::HideComponent (лишається видимою лише в основній камері).
 *
 * Підписується на ACesiumGeoreference::OnGeoreferenceUpdated і перераховує позицію з
 * поточного StoredLLH щоразу, коли origin georeference змінюється. Це критично на старті
 * гри: якщо цю зону спавнять (напр. AEnvironmentActorManager::RefreshEWZones) РАНІШЕ, ніж
 * інший актор (UEnvironmentSectionWidget у NativeConstruct) застосує збережений origin —
 * BeginPlay різних акторів виконується в незагарантованому порядку — то ApplyGeoPosition()
 * одразу після спавну порахує позицію зі старого/дефолтного origin і зона опиниться не там.
 * Підписка сама себе виправляє щойно origin таки виставлять правильно.
 */
UCLASS()
class UAVSIMULATOR_API AEWZoneActor : public AActor
{
	GENERATED_BODY()

public:
	AEWZoneActor();

	/** Радіус зони РЕБ, см (Unreal-одиниці). Змінює масштаб SphereVisual. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW Zone")
	float Radius = 5000.0f;

	/** Прозорий матеріал сфери (Blend Mode = Translucent). Признач вручну в редакторі. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW Zone")
	TObjectPtr<UMaterialInterface> ZoneMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EW Zone")
	TObjectPtr<UStaticMeshComponent> SphereVisual;

	/** Задає радіус і одразу перераховує масштаб SphereVisual. */
	UFUNCTION(BlueprintCallable, Category = "EW Zone")
	void SetRadius(float NewRadius);

	/** Довгота (X) реальної точки глобуса, де стоїть зона, у градусах. */
	UFUNCTION(BlueprintPure, Category = "EW Zone")
	double GetLongitude() const { return StoredLongitude; }

	/** Широта (Y) реальної точки глобуса, де стоїть зона, у градусах. */
	UFUNCTION(BlueprintPure, Category = "EW Zone")
	double GetLatitude() const { return StoredLatitude; }

	/** Переміщує зону на нову довготу, зберігаючи поточну широту й висоту. */
	UFUNCTION(BlueprintCallable, Category = "EW Zone")
	void SetLongitude(double NewLongitude);

	/** Переміщує зону на нову широту, зберігаючи поточну довготу й висоту. */
	UFUNCTION(BlueprintCallable, Category = "EW Zone")
	void SetLatitude(double NewLatitude);

	/** Атомарно задає довготу/широту/висоту одним викликом і застосовує позицію одним
	 *  перерахунком — на відміну від послідовних SetLongitude/SetLatitude/SetHeight, тут
	 *  немає проміжного стану з невалідним (частково старим) LLH-набором. Використовується
	 *  AEnvironmentActorManager при синхронізації з EWConfigurations. */
	UFUNCTION(BlueprintCallable, Category = "EW Zone")
	void SetGeoPosition(double NewLongitude, double NewLatitude, double NewHeight);

	/** Інтенсивність перешкод РЕБ у заданій світовій точці, [0,1]: 1.0 у центрі зони,
	 *  лінійно спадає до 0.0 на межі Radius і лишається 0.0 за нею. Єдине місце, де рахується
	 *  ця формула — консюменти (напр. UUAVCameraComponent) лише викликають її для кожної
	 *  зони й беруть максимум. */
	UFUNCTION(BlueprintPure, Category = "EW Zone")
	float GetInterferenceIntensity(const FVector& WorldLocation) const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	void ApplyRadius();

	/** ACesiumGeoreference::OnGeoreferenceUpdated — перераховує позицію з поточного
	 *  StoredLLH проти щойно оновленого origin (див. коментар класу). */
	UFUNCTION()
	void OnGeoreferenceUpdated();

	ACesiumGeoreference* ResolveGeoreference() const;

	/** Рахує Unreal-позицію з поточних StoredLongitude/StoredLatitude/StoredHeight одним
	 *  атомарним викликом Cesium-утиліти й переставляє туди актора. */
	void ApplyGeoPosition();

	/** Джерело правди для позиції зони, градуси/метри — НЕ похідне від GetActorLocation().
	 *  Ініціалізується один раз у BeginPlay зі стартової (завжди валідної) позиції актора. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EW Zone", meta = (AllowPrivateAccess = "true"))
	double StoredLongitude = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EW Zone", meta = (AllowPrivateAccess = "true"))
	double StoredLatitude = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EW Zone", meta = (AllowPrivateAccess = "true"))
	double StoredHeight = 0.0;

	/** Радіус /Engine/BasicShapes/Sphere.Sphere при масштабі (1,1,1), см. */
	static constexpr float BaseSphereRadiusCm = 50.0f;
};
