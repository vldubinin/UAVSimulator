#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "CameraAltitudeComponent.generated.h"

/**
 * Camera altitude sensor: reads the world-space Z position of the onboard
 * USceneCaptureComponent2D each tick and publishes it in metres.
 *
 * Implements IUAVSensorInterface — SensorBusComponent auto-discovers this
 * component and calls GetLatestFrame() each bus tick.
 *
 * Payload format: {"altitude_m": <float>}
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCameraAltitudeComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UCameraAltitudeComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("camera_altitude"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/** Current camera altitude in metres (world-space Z), updated every tick. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera Altitude")
	float LatestAltitudeMeters = 0.0f;

private:
	UPROPERTY()
	USceneCaptureComponent2D* CaptureComp = nullptr;

	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
