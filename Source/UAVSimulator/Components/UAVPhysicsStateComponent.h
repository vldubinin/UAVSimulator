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

	/**
	 * Оновлює всі кешовані значення фізичного стану з StaticMeshComponent власника.
	 * Якщо фізична симуляція вимкнена — кутова швидкість та центр мас скидаються до нуля.
	 * Напрямок набігаючого потоку завжди оновлюється з вектора швидкості актора.
	 * Необхідно викликати один раз на початку кожного тіку перед зчитуванням геттерів.
	 */
	void Update();

	/** @return Лінійна швидкість mesh у cm/s (світові координати). */
	FVector GetLinearVelocity()   const { return LinearVelocity;        }

	/** @return Кутова швидкість mesh у рад/с (світові координати). */
	FVector GetAngularVelocity()  const { return AngularVelocity;       }

	/** @return Центр мас mesh у світових координатах (cm). */
	FVector GetCenterOfMass()     const { return CenterOfMassInWorld;   }

	/** @return Нормалізований вектор набігаючого потоку (протилежний до вектора швидкості); нульовий вектор якщо ЛА стоїть на місці. */
	FVector GetAirflowDirection() const { return AirflowDirection;      }

private:
	FVector LinearVelocity;
	FVector AngularVelocity;
	FVector CenterOfMassInWorld;
	FVector AirflowDirection;
};
