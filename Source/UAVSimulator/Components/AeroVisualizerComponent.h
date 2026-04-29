#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"

#include "AeroVisualizerComponent.generated.h"

class UFlightDynamicsComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UAeroVisualizerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UAeroVisualizerComponent();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "VFX")
	UNiagaraSystem* WakeVortexSystemAsset;

	UPROPERTY(Transient)
	UNiagaraComponent* WakeVortexComponent;

	UPROPERTY(Transient)
	UFlightDynamicsComponent* FlightDynamicsRef;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};