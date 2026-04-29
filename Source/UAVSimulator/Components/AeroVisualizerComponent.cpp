#include "AeroVisualizerComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"

UAeroVisualizerComponent::UAeroVisualizerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UAeroVisualizerComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	FlightDynamicsRef = Owner->FindComponentByClass<UFlightDynamicsComponent>();

	if (FlightDynamicsRef)
	{
		AddTickPrerequisiteComponent(FlightDynamicsRef);
	}

	if (WakeVortexSystemAsset)
	{
		WakeVortexComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			WakeVortexSystemAsset,
			this,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			true);
	}
}

void UAeroVisualizerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!FlightDynamicsRef || !WakeVortexComponent) return;

	const FVector WingtipWorld = FlightDynamicsRef->GetLeftWingtipWorldPosition();
	const float AoA = FlightDynamicsRef->GetAngleOfAttack();

	const FVector WingtipLocal = GetOwner()->GetActorTransform().InverseTransformPosition(WingtipWorld);

	WakeVortexComponent->SetVariableVec3(FName("VortexLocalPosition"), WingtipLocal);
	WakeVortexComponent->SetVariableFloat(FName("Intensity"), AoA);
}
