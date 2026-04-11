#include "UAVPhysicsStateComponent.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"

UUAVPhysicsStateComponent::UUAVPhysicsStateComponent()
{
	// Компонент лише читає дані — власний тік не потрібен
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
		// Зчитуємо фізичні дані лише коли симуляція активна
		LinearVelocity      = Mesh->GetPhysicsLinearVelocity();         // cm/s
		AngularVelocity     = Mesh->GetPhysicsAngularVelocityInRadians(); // рад/с
		CenterOfMassInWorld = Mesh->GetCenterOfMass();                  // cm, світові координати
	}
	else
	{
		// При вимкненій симуляції кутова швидкість і центр мас не визначені
		AngularVelocity     = FVector::ZeroVector;
		CenterOfMassInWorld = FVector::ZeroVector;
	}

	// Напрямок набігаючого потоку — завжди протилежний вектору швидкості
	const FVector ActorVelocity = Owner->GetVelocity();
	AirflowDirection = ActorVelocity.IsNearlyZero()
		? FVector::ZeroVector
		: -ActorVelocity.GetSafeNormal();
}
