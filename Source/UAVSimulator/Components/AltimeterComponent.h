#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "AltimeterComponent.generated.h"

/**
 * Altimeter sensor: reads the aircraft's world-space altitude (Z position)
 * and publishes it as a JSON payload each tick.
 *
 * Implements IUAVSensorInterface — SensorBusComponent auto-discovers this
 * component and calls GetLatestFrame() each bus tick.
 *
 * Payload format: {"altitude_m": <float>}
 * Value is in metres (converted from Unreal cm).
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UAltimeterComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UAltimeterComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("altimeter"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/** Current altitude in metres, updated every tick. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Altimeter")
	float LatestAltitudeMeters = 0.0f;

private:
	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
