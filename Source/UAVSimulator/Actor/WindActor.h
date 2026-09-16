#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WindActor.generated.h"

class UStaticMeshComponent;
class UArrowComponent;
class UMaterialInterface;
class ACesiumGeoreference;

/**
 * Наочний маркер вітрового вектора у персистентному рівні: паралелепіпед (розтягнутий
 * куб), що з'єднує початкову й кінцеву геоточки вектора. Спавниться і позиціюється
 * виключно через AEnvironmentActorManager::RefreshWindVectors() з
 * AEnvironmentActorManager::WindConfigurations — так само, як AEWZoneActor для
 * EWConfigurations. Наразі суто візуальний — жодного впливу на політ літака.
 *
 * StoredStart.../StoredEnd... (Longitude/Latitude/Height, градуси/метри) — єдине джерело
 * правди для положення, за тим самим принципом, що й StoredLongitude/Latitude/Height
 * в AEWZoneActor: позиція рахується наперед через
 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal для обох точок,
 * актор ставиться в їхню середину (BoxVisual — куб, тож рівномірно розтягується в обидва
 * боки від центру), а BoxVisual обертається й масштабується вздовж напрямку Start->End.
 *
 * Перед паралелепіпеда рахується так, наче він описаний навколо циліндра радіусом
 * Radius, вісь якого — відрізок Start->End: переріз (Y/Z) — квадрат зі стороною
 * 2*Radius (діаметр циліндра), довжина (X) — відстань Start-End.
 *
 * Підписується на ACesiumGeoreference::OnGeoreferenceUpdated і перераховує позицію з
 * поточних Stored* щоразу, коли origin georeference змінюється — той самий захист від
 * непередбачуваного порядку BeginPlay між акторами, що й в AEWZoneActor (детальніше —
 * коментар класу AEWZoneActor).
 *
 * Приховується з кожного онбордового SceneCaptureComponent2D через
 * UUAVCameraComponent::HideComponent (BoxVisual і ArrowVisual — лишається видимим лише
 * в основній камері), так само як AEWZoneActor::SphereVisual.
 */
UCLASS()
class UAVSIMULATOR_API AWindActor : public AActor
{
	GENERATED_BODY()

public:
	AWindActor();

	/** Радіус вектора, см — переріз (Y/Z) паралелепіпеда рахується як квадрат
	 *  описаний навколо циліндра цього радіуса (сторона = 2*Radius). Довжина (X)
	 *  рахується з відстані між Start і End. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float Radius = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	TObjectPtr<UMaterialInterface> WindMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind")
	TObjectPtr<UStaticMeshComponent> BoxVisual;

	/** Стрілка напрямку вектора (вздовж локального +X, тобто Start->End) — дочірній
	 *  компонент BoxVisual, тож масштабується разом з ним (довжина/переріз). На відміну
	 *  від типового використання UArrowComponent як суто редакторського гізмо,
	 *  bHiddenInGame тут вимкнено в конструкторі, щоб стрілка була видима й під час гри. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind")
	TObjectPtr<UArrowComponent> ArrowVisual;

	/** Атомарно задає обидві геоточки (Start і End) вектора одним викликом і одразу
	 *  перераховує позицію/орієнтацію/масштаб BoxVisual. Використовується
	 *  AEnvironmentActorManager при синхронізації з WindConfigurations. */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	void SetGeoPositions(double NewStartLongitude, double NewStartLatitude, double NewStartHeight,
		double NewEndLongitude, double NewEndLatitude, double NewEndHeight);

	/** Задає радіус (см) і одразу перераховує переріз BoxVisual. */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	void SetRadius(float NewRadius);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	/** ACesiumGeoreference::OnGeoreferenceUpdated — перераховує позицію з поточних
	 *  Stored* проти щойно оновленого origin (див. коментар класу). */
	UFUNCTION()
	void OnGeoreferenceUpdated();

	ACesiumGeoreference* ResolveGeoreference() const;

	/** Рахує світові позиції Start/End з поточних Stored*, ставить актора в їхню
	 *  середину і обертає/масштабує BoxVisual (довжина — з відстані Start-End,
	 *  переріз — з Radius) вздовж напрямку Start->End. Єдине місце, де рахується
	 *  повний трансформ — викликається і з SetGeoPositions, і з SetRadius, бо обидва
	 *  впливають на масштаб (X проти Y/Z), а не лише на щось одне окремо. */
	void ApplyTransform();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind", meta = (AllowPrivateAccess = "true"))
	double StoredStartLongitude = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind", meta = (AllowPrivateAccess = "true"))
	double StoredStartLatitude = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind", meta = (AllowPrivateAccess = "true"))
	double StoredStartHeight = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind", meta = (AllowPrivateAccess = "true"))
	double StoredEndLongitude = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind", meta = (AllowPrivateAccess = "true"))
	double StoredEndLatitude = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind", meta = (AllowPrivateAccess = "true"))
	double StoredEndHeight = 0.0;

	/** Розмір /Engine/BasicShapes/Cube.Cube при масштабі (1,1,1), см. */
	static constexpr float BaseCubeSizeCm = 100.0f;
};
