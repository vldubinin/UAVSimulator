#include "UAVPhysicsStateComponent.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"

UUAVPhysicsStateComponent::UUAVPhysicsStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	LinearVelocity      = FVector::ZeroVector;
	AngularVelocity     = FVector::ZeroVector;
	CenterOfMassInWorld = FVector::ZeroVector;
	AirflowDirection    = FVector::ZeroVector;
}

void UUAVPhysicsStateComponent::Update()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	UStaticMeshComponent* Mesh = Owner->FindComponentByClass<UStaticMeshComponent>();
	if (Mesh && Mesh->IsSimulatingPhysics())
	{
		LinearVelocity      = Mesh->GetPhysicsLinearVelocity();
		AngularVelocity     = Mesh->GetPhysicsAngularVelocityInRadians();
		CenterOfMassInWorld = Mesh->GetCenterOfMass();
	}
	else
	{
		AngularVelocity     = FVector::ZeroVector;
		CenterOfMassInWorld = FVector::ZeroVector;
	}

	const FVector ActorVelocity = Owner->GetVelocity();
	AirflowDirection = ActorVelocity.IsNearlyZero()
		? FVector::ZeroVector
		: -ActorVelocity.GetSafeNormal();
}
