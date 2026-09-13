#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "DronePositionComponent.generated.h"

/**
 * Сенсор позиції: читає світову позицію літака і щотіку публікує
 * її як JSON-корисне навантаження.
 *
 * Реалізує IUAVSensorInterface — SensorBusComponent автоматично знаходить цей
 * компонент і викликає GetLatestFrame() на кожному тіку шини.
 *
 * Формат корисного навантаження: {"x_m": <float>, "y_m": <float>, "z_m": <float>}
 * Значення у метрах (конвертовані з см Unreal).
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UDronePositionComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UDronePositionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("drone_position"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/** Поточна позиція у світових координатах в метрах, оновлюється щотіку. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone Position")
	FVector LatestPositionMeters = FVector::ZeroVector;

private:
	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
