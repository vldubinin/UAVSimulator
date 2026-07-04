#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "DronePositionComponent.generated.h"

/**
 * Position sensor: reads the aircraft's world-space location and publishes
 * it as a JSON payload each tick.
 *
 * Implements IUAVSensorInterface — SensorBusComponent auto-discovers this
 * component and calls GetLatestFrame() each bus tick.
 *
 * Payload format: {"x_m": <float>, "y_m": <float>, "z_m": <float>}
 * Values are in metres (converted from Unreal cm).
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

	/** Current world-space position in metres, updated every tick. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone Position")
	FVector LatestPositionMeters = FVector::ZeroVector;

private:
	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
