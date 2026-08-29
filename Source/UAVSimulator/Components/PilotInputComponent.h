#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PilotInputComponent.generated.h"

class UFlightDynamicsComponent;

/** Модель поведінки газу для інкрементних (не абсолютних) джерел. */
UENUM(BlueprintType)
enum class EPilotThrottleModel : uint8
{
	/** Вісь додає до акумулятора: стик угору = газ росте поки тримаєш, центр = газ тримається. */
	Incremental UMETA(DisplayName = "Інкрементний (вісь додає, газ утримується)"),
	/** Вісь напряму = положення газу: out = вісь*0.5 + 0.5. */
	Absolute    UMETA(DisplayName = "Абсолютний (вісь = положення газу 0..1)")
};

/**
 * Координатор керування пілота. Авто-дискаверить усі компоненти-джерела (IPilotInputSource)
 * на власнику — так само, як USensorBusComponent робить з IUAVSensorInterface — щокадру
 * опитує їх, зводить у єдину команду й лишається ЄДИНИМ записувачем у UFlightDynamicsComponent
 * (UpdateAileron/Elevator/Rudder/ThrottleControl).
 *
 * Зведення: серед активних джерел береться лише тир пріоритету з найвищим числом; у його
 * межах осі підсумовуються з clamp[-1,1]. Тож активний автопілот (tier 100) повністю
 * перебиває клавіатуру/джойстик (tier 0).
 *
 * Порядок тіку (ставиться в AAirplane::BeginPlay через prerequisite):
 *   AAirplane::Tick -> UAttitudeControlComponent (ZMQ+PID, лише обчислення)
 *                   -> UPilotInputComponent (це) -> UFlightDynamicsComponent (споживання).
 * Потрібно бо UFlightDynamicsComponent обнуляє ControlState наприкінці власного тіку.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UPilotInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPilotInputComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Явний список джерел (кожне мусить реалізувати IPilotInputSource). Порожньо => авто-дискавер
	 * усіх IPilotInputSource-компонентів на власнику в BeginPlay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot Input")
	TArray<TObjectPtr<UActorComponent>> InputSources;

	/**
	 * Спільна для всіх джерел інверсія тангажу (властивість схеми керма планера, а не пристрою).
	 * true (авіаційний стандарт): ручку/стик на себе (вниз) = ніс угору.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot Input")
	bool bInvertPitch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot Input|Throttle")
	EPilotThrottleModel ThrottleModel = EPilotThrottleModel::Incremental;

	/** Частка ходу газу за секунду при повному відхиленні осі (Incremental). 0.5 => 0..1 за 2 с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot Input|Throttle", meta = (ClampMin = "0.0"))
	float ThrottleRampRate = 0.5f;

	/** Поточне положення газу [0..1], утримується між кадрами. Єдине місце стану газу пілота. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pilot Input|Throttle")
	float Throttle01 = 0.f;

	/** Друкувати в лог активний тир, зведені осі та Throttle01 щотіку (LogUAV) + вивід на екран. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot Input")
	bool bLogInputDebug = false;

protected:
	virtual void BeginPlay() override;

private:
	/** true лише коли власник — локально керований APawn і є UFlightDynamicsComponent. */
	bool ShouldDrive() const;

	UPROPERTY()
	UFlightDynamicsComponent* FlightDynamics = nullptr;

	/** Розв'язано в BeginPlay; кожне реалізує IPilotInputSource. */
	TArray<TWeakObjectPtr<UActorComponent>> ResolvedSources;
};
