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
 * EWConfigurations. Впливає на політ літака через GetWindVelocityAtLocation() —
 * її щотіку опитує USubAerodynamicSurfaceSC (для кожного сегмента поверхні окремо,
 * у власній точці центру тиску) через UUAVSimulationSubsystem::GetWindVelocityAtLocation(),
 * яка сумує внесок усіх AWindActor на сцені. Сам актор — єдине джерело правди щодо
 * того, чи впливає він на задану світову точку, з яким напрямком і величиною.
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

	/** Швидкість вітру, см/с (Unreal-native) — НЕ ті самі одиниці, що
	 *  FWindVectorConfiguration::Speed (м/с); конвертація ×100 відбувається в
	 *  AEnvironmentActorManager::RefreshWindVectors(), за тим самим принципом, що й для
	 *  Radius/Config.Radius. Напрямок — GetActorForwardVector() (Start->End, вже
	 *  виставляється ApplyTransform()). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float Speed = 0.0f;

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

	/** Задає швидкість вітру (см/с). Перерахунку геометрії не потребує. */
	UFUNCTION(BlueprintCallable, Category = "Wind")
	void SetSpeed(float NewSpeed);

	/** Внесок цього вектора у швидкість вітру в заданій світовій точці, см/с. Сам актор —
	 *  єдине джерело правди щодо належності точки до зони (BoxVisual — root-компонент,
	 *  тож GetActorTransform() уже включає його нерівномірний масштаб — переведена в
	 *  локальний простір точка одразу опиняється в НЕмасштабованому просторі куба,
	 *  півсторона = BaseCubeSizeCm*0.5), напрямку (GetActorForwardVector()) і плавного
	 *  загасання біля межі (щоб уникнути стрибка сили, коли сегмент перетинає межу
	 *  зони) — ніхто зовні не дублює цю формулу. Повертає FVector::ZeroVector поза
	 *  зоною (і поза її пом'якшеною межею). */
	UFUNCTION(BlueprintPure, Category = "Wind")
	FVector GetWindVelocityAtLocation(const FVector& WorldLocation) const;

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

	/** Частка (від півдовжини кожної осі, у нормалізованому локальному просторі), на якій
	 *  внесок вітру плавно згасає до нуля біля межі боксу — не для UI. */
	static constexpr float EdgeSoftnessFrac = 0.15f;
};
