#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "CameraInclinationComponent.generated.h"

/**
 * Сенсор нахилу камери: читає фактичний кут вертикального нахилу бортової
 * камери (USceneCaptureComponent2D) у світових координатах щотіку.
 *
 * Значення pitch подається у світовому просторі — не відносно літака, — тому
 * воно враховує і відхилення підвісу (гімбала), і власну орієнтацію літака.
 * Додатне значення = камера дивиться вгору, від'ємне = камера дивиться вниз.
 *
 * Реалізує IUAVSensorInterface — SensorBusComponent автоматично знаходить цей
 * компонент і викликає GetLatestFrame() на кожному тіку шини.
 *
 * Формат корисного навантаження: {"pitch_deg": <float>}
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

	/** Поточний тангаж камери у світових координатах, у градусах. Додатне = вгору, від'ємне = вниз. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera Inclination")
	float LatestPitchDeg = 0.0f;

private:
	UPROPERTY()
	USceneCaptureComponent2D* CaptureComp = nullptr;

	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
