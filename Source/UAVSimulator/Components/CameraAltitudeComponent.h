#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "CameraAltitudeComponent.generated.h"

/**
 * Сенсор висоти камери: читає світову позицію Z бортового
 * USceneCaptureComponent2D щотіку і публікує її в метрах.
 *
 * Реалізує IUAVSensorInterface — SensorBusComponent автоматично знаходить цей
 * компонент і викликає GetLatestFrame() на кожному тіку шини.
 *
 * Формат корисного навантаження: {"altitude_m": <float>}
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

	/** Поточна висота камери в метрах (світова координата Z), оновлюється щотіку. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera Altitude")
	float LatestAltitudeMeters = 0.0f;

private:
	UPROPERTY()
	USceneCaptureComponent2D* CaptureComp = nullptr;

	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
