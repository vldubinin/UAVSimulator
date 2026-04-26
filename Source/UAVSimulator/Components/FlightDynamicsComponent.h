// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "UAVSimulator/SceneComponent/ControlSurface/ControlSurfaceSC.h"
#include "UAVSimulator/Util/AerodynamicPhysicalCalculationUtil.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
#include "UAVSimulator/Entity/ControlInputState.h"
#include "UAVSimulator/Entity/VortexEntities.h"
#include "UAVSimulator/Components/UAVPhysicsStateComponent.h"

#include "FlightDynamicsComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UCurveFloat;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UAVSIMULATOR_API UFlightDynamicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightDynamicsComponent();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Engine")
	float MaxStaticThrust = 1500000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Engine")
	float EngineSpoolSpeed = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Engine")
	UCurveFloat* ThrustVsAirspeedCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	FVector EngineThrustOffsetLocal = FVector(0.f, 0.f, 0.f);

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Replicates the old OnConstruction editor visualization: gathers surfaces, initializes CoM, draws thrust-point markers. */
	void UpdateEditorVisualization(class UStaticMeshComponent* Mesh);

	FControlInputState GetControlState() const { return ControlState; }

	UFUNCTION(BlueprintCallable, CallInEditor, meta = (DisplayName = "Розрахувати поляри для ЛА"), Category = "Автоматизація")
	void GenerateAerodynamicPhysicalConfigutation();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings",
		meta = (ToolTip = "Значення '1' відповідає звичайній швидкості.", DisplayName = "Швидкість роботи симуляції"))
	float DebugSimulatorSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings",
		meta = (ToolTip = "Увімкнути/вимкнути димові лінії (Niagara)"))
	bool bVisualizeParticles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings",
		meta = (ToolTip = "Увімкнути/вимкнути малювання векторів сил та моментів (Debug Arrows)"))
	bool bVisualizeForces = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings",
		meta = (ToolTip = "Початкова швидкість літака при старті симуляції (м/с)"))
	float InitialSpeedMs = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	float TargetThrottle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Engine")
	float CurrentThrottle = 0.0f;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateAileronControl"), Category = "Control")
	void UpdateAileronControl(float LeftAileronAngle, float RightAileronAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateElevatorControl"), Category = "Control")
	void UpdateElevatorControl(float LeftElevatorAngle, float RightElevatorAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateRudderControl"), Category = "Control")
	void UpdateRudderControl(float RudderAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateThrottleControl"), Category = "Control")
	void UpdateThrottleControl(float Throttle);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VLM",
		meta = (ToolTip = "Шаблон Niagara системи для генерації на крилах"))
	UNiagaraSystem* FlowVisualizerSystem;

	UPROPERTY(Transient)
	TArray<UNiagaraComponent*> ActiveFlowVisualizers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VLM",
		meta = (ToolTip = "Максимальна кількість вузлів в одній лінії вихорового сліду."))
	int32 MaxWakeLength = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VLM",
		meta = (ToolTip = "Мінімальна відстань (см) між вузлами сліду."))
	float MinWakeDistance = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VLM",
		meta = (ToolTip = "Щільність повітря в кг/м³ для розрахунку циркуляції Γ."))
	float AirDensity = 1.225f;

private:
	UPROPERTY()
	UUAVPhysicsStateComponent* PhysicsState;

	TArray<UAerodynamicSurfaceSC*> Surfaces;
	TArray<UControlSurfaceSC*>     ControlSurfaces;

	FControlInputState ControlState;

	/** Bound vortex filaments rebuilt each tick from active surfaces. */
	TArray<FBoundVortex> CurrentBoundVortices;

	/**
	 * Trailing vortex wake lines shed from each surface.
	 * Outer index = wake line (2 per surface: root + tip); inner = sequential nodes.
	 */
	TArray<TArray<FTrailingVortexNode>> VortexWakeLines;

	void UpdateVortexWake();
	void SendWakeDataToNiagara();
	FVector GetInducedVelocity(const FVector& TargetPosCm) const;
};
