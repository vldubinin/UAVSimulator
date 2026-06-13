#include "SegmentationMaskCameraComponent.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"

USegmentationMaskCameraComponent::USegmentationMaskCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USegmentationMaskCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	CameraComp = Owner->FindComponentByClass<UUAVCameraComponent>();
	if (!CameraComp)
	{
		UE_LOG(LogUAV, Error, TEXT("SegmentationMaskCameraComponent: UUAVCameraComponent not found on %s."), *Owner->GetName());
	}
}

bool USegmentationMaskCameraComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bSensorEnabled || !CameraComp) return false;

	TArray<uint8> Payload;
	double Timestamp;
	if (!CameraComp->GetMaskFrame(Payload, Timestamp)) return false;

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = Timestamp;
	OutFrame.Payload   = MoveTemp(Payload);
	return true;
}
