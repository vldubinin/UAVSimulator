#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "AttitudeIndicatorComponent.generated.h"

/**
 * Сенсор авіагоризонту: читає просторову орієнтацію літака —
 * крен (roll), тангаж (pitch), рискання (yaw) — та відповідні
 * кутові швидкості корпусу, публікуючи їх як JSON-корисне навантаження щотіку.
 *
 * Реалізує IUAVSensorInterface — SensorBusComponent автоматично знаходить цей
 * компонент і викликає GetLatestFrame() на кожному тіку шини.
 *
 * Формат корисного навантаження:
 *   {
 *     "roll_deg":  <float>,  "pitch_deg":  <float>,  "yaw_deg":  <float>,
 *     "roll_rate_dps": <float>, "pitch_rate_dps": <float>, "yaw_rate_dps": <float>
 *   }
 * Кути беруться з GetOwner()->GetActorRotation(); швидкості — з фізичної кутової
 * швидкості UStaticMeshComponent власника (0, якщо фізика не симулюється).
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
