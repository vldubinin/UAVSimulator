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
 * рівні — так само, як ACesiumGeoreference/ACesiumSunSky — і читається звідти
 * UEnvironmentSectionWidget (Longitude/Latitude/Radius) та
 * AUAVSimulatorGameModeBase::UpdateEWSettings().
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
 * GetActorLocation() лишається справжньою Unreal world-позицією, яку й далі читає
 * AUAVSimulatorGameModeBase::UpdateEWSettings() для рахунку відстані до літака.
 *
 * Приховується з кожного онбордового SceneCaptureComponent2D через
 * UUAVCameraComponent::HideComponent (лишається видимою лише в основній камері).
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

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	void ApplyRadius();

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
