#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"

#include "SegmentationMaskCameraComponent.generated.h"

class UUAVCameraComponent;

/**
 * Адаптер-датчик, що публікує потік маски сегментації у топіку
 * "segmentation_mask". Делегує отримання маски UAVCameraComponent,
 * якому належить пайплайн захоплення й кодування.
 *
 * Потребує, щоб UAVCameraComponent::MaskPostProcessMaterial було встановлено на тому самому акторі.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API USegmentationMaskCameraComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	USegmentationMaskCameraComponent();

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("segmentation_mask"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	UUAVCameraComponent* CameraComp = nullptr;
};
