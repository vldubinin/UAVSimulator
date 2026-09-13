#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "AltimeterComponent.generated.h"

/**
 * Сенсор висотоміра: читає висоту літака у світових координатах (позиція Z)
 * і щотіку публікує її як JSON-корисне навантаження.
 *
 * Реалізує IUAVSensorInterface — SensorBusComponent автоматично знаходить цей
 * компонент і викликає GetLatestFrame() на кожному тіку шини.
 *
 * Формат корисного навантаження: {"altitude_m": <float>}
 * Значення у метрах (конвертоване з см Unreal).
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

	/** Поточна висота в метрах, оновлюється щотіку. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Altimeter")
	float LatestAltitudeMeters = 0.0f;

private:
	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
