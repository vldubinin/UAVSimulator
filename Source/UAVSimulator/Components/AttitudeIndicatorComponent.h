#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "AttitudeIndicatorComponent.generated.h"

/**
 * Attitude Indicator sensor: reads the aircraft's body attitude —
 * roll (крен), pitch (тангаж), yaw (рискання) — and the corresponding
 * body angular rates, publishing them as a JSON payload each tick.
 *
 * Implements IUAVSensorInterface — SensorBusComponent auto-discovers this
 * component and calls GetLatestFrame() each bus tick.
 *
 * Payload format:
 *   {
 *     "roll_deg":  <float>,  "pitch_deg":  <float>,  "yaw_deg":  <float>,
 *     "roll_rate_dps": <float>, "pitch_rate_dps": <float>, "yaw_rate_dps": <float>
 *   }
 * Angles come from GetOwner()->GetActorRotation(); rates from the owner's
 * UStaticMeshComponent physics angular velocity (0 if physics is not simulating).
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UAttitudeIndicatorComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UAttitudeIndicatorComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("attitude_indicator"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/** Крен, тангаж, рискання у градусах, оновлюються щотіку. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude Indicator")
	float LatestRollDeg = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude Indicator")
	float LatestPitchDeg = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude Indicator")
	float LatestYawDeg = 0.0f;

	/** Кутові швидкості корпусу у град/с (світові осі X/Y/Z). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude Indicator")
	FVector LatestAngularRateDps = FVector::ZeroVector;

private:
	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
