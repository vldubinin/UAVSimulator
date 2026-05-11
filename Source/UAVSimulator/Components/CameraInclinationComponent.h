#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "CameraInclinationComponent.generated.h"

/**
 * Camera inclination sensor: reads the actual world-space vertical tilt angle
 * of the onboard camera (USceneCaptureComponent2D) each tick.
 *
 * Reports pitch in world space — not relative to the aircraft — so the value
 * accounts for both the gimbal deflection and the aircraft's own attitude.
 * Positive = camera pointing upward, negative = camera pointing downward.
 *
 * Implements IUAVSensorInterface — SensorBusComponent auto-discovers this
 * component and calls GetLatestFrame() each bus tick.
 *
 * Payload format: {"pitch_deg": <float>}
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCameraInclinationComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UCameraInclinationComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("camera_inclination"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/** Current world-space camera pitch in degrees. Positive = up, negative = down. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera Inclination")
	float LatestPitchDeg = 0.0f;

private:
	UPROPERTY()
	USceneCaptureComponent2D* CaptureComp = nullptr;

	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
