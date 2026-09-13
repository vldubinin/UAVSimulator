#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"

#include "CameraFrameComponent.generated.h"

class UUAVCameraComponent;

/**
 * Адаптер-сенсор, що публікує RGB-потік UAVCameraComponent на топіку
 * "camera". Додавати поруч з UAVCameraComponent і SensorBusComponent
 * на тому самому акторі.
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
