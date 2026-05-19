#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"

#include "CameraFrameComponent.generated.h"

class UUAVCameraComponent;

/**
 * Sensor adapter that exposes the UAVCameraComponent's RGB stream on the
 * "camera" topic. Add this alongside UAVCameraComponent and SensorBusComponent
 * on the same actor.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCameraFrameComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UCameraFrameComponent();

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("camera"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	UUAVCameraComponent* CameraComp = nullptr;
};
