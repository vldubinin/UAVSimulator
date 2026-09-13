#include "CameraFrameComponent.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"

UCameraFrameComponent::UCameraFrameComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCameraFrameComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	CameraComp = Owner->FindComponentByClass<UUAVCameraComponent>();
}

bool UCameraFrameComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bSensorEnabled || !CameraComp) return false;

	TArray<uint8> Payload;
	double Timestamp;
	if (!CameraComp->GetRGBFrame(Payload, Timestamp)) return false;

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = Timestamp;
	OutFrame.Payload   = MoveTemp(Payload);
	return true;
}
