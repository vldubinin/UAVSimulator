#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVPhysicsStateComponent.generated.h"

/**
 * Tracks per-tick physics state for the aircraft: linear/angular velocity,
 * center of mass, and airflow direction.
 * Call Update() once per tick before reading any getter.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UUAVPhysicsStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUAVPhysicsStateComponent();

	/** Refresh all physics state values from the owner's static mesh component. Call from Tick. */
	void Update();

	FVector GetLinearVelocity()   const { return LinearVelocity;        }
	FVector GetAngularVelocity()  const { return AngularVelocity;       }
	FVector GetCenterOfMass()     const { return CenterOfMassInWorld;   }
	FVector GetAirflowDirection() const { return AirflowDirection;      }

private:
	FVector LinearVelocity;
	FVector AngularVelocity;
	FVector CenterOfMassInWorld;
	FVector AirflowDirection;
};
