// Заповніть примітку про авторські права на сторінці Description в Project Settings.

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

	/** Відтворює стару логіку OnConstruction для візуалізації в редакторі: збирає поверхні, ініціалізує центр мас, малює маркери точки тяги. */
	void UpdateEditorVisualization(class UStaticMeshComponent* Mesh);

	FControlInputState GetControlState() const { return ControlState; }

	float GetAngleOfAttack() const;
	FVector GetLeftWingtipWorldPosition() const;
	/** @return Світова позиція правого кінця крила (протилежна GetLeftWingtipWorldPosition, той самий елемент Surfaces[0]). */
	FVector GetRightWingtipWorldPosition() const { return CurrentRightWingtipWorldPos; }

	/**
	 * @return Розмах першої поверхні (Surfaces[0]) за сирими даними SurfaceForm (Offset.Y), в см,
	 * БЕЗ урахування поточного масштабу актора — "дизайнерський" розмір із конфігурації.
	 * Використовується для авто-калібрування масштабу актора при спавні, див. AAirplane::BeginPlay.
	 */
	float GetDesignWingSpanCm() const;

	/** @return Повітряна швидкість (модуль лінійної швидкості mesh), м/с. */
	float GetAirspeed() const { return PhysicsState->GetLinearVelocity().Size() / 100.0f; }
	/** @return Та сама швидкість у км/год — зручно для порівняння з крейсерською. */
	float GetAirspeedKmh() const { return GetAirspeed() * 3.6f; }
	const TArray<TArray<FTrailingVortexNode>>& GetVortexWakeLines() const { return VortexWakeLines; }

	UFUNCTION(BlueprintCallable, CallInEditor, meta = (DisplayName = "Розрахувати поляри для ЛА"), Category = "Автоматизація")
	void GenerateAerodynamicPhysicalConfigutation();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings",
		meta = (ToolTip = "Значення '1' відповідає звичайній швидкості.", DisplayName = "Швидкість роботи симуляції"))
	float DebugSimulatorSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings",
		meta = (ToolTip = "Увімкнути/вимкнути малювання векторів сил та моментів (Debug Arrows)"))
	bool bVisualizeForces = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings",
		meta = (ToolTip = "Друкувати в лог справжню швидкість, тягу та баланс сил кожні 0.5 с (LogUAV)"))
	bool bLogFlightDebug = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings",
		meta = (ToolTip = "Початкова швидкість літака при старті симуляції (м/с)"))
	float InitialSpeedMs = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	float TargetThrottle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Engine")
	float CurrentThrottle = 0.0f;

	/** Фактична тяга двигуна цього тіку (Н) — MaxStaticThrust * CurrentThrottle * ThrustVsAirspeedCurve(V). 0, поки двигун не розкручений (CurrentThrottle <= 0.01). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Engine")
	float CurrentThrustN = 0.0f;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateAileronControl"), Category = "Control")
	void UpdateAileronControl(float LeftAileronAngle, float RightAileronAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateElevatorControl"), Category = "Control")
	void UpdateElevatorControl(float LeftElevatorAngle, float RightElevatorAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateRudderControl"), Category = "Control")
	void UpdateRudderControl(float RudderAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateThrottleControl"), Category = "Control")
	void UpdateThrottleControl(float Throttle);

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

	/** Приєднані вихрові нитки, що перебудовуються щотіку з активних поверхонь. */
	TArray<FBoundVortex> CurrentBoundVortices;

	/** Світова позиція правого кінця крила (Surfaces[0]), оновлюється кожен тік поруч із CurrentBoundVortices. */
	FVector CurrentRightWingtipWorldPos = FVector::ZeroVector;

	/**
	 * Лінії сліду хвостових вихорів, що сходять з кожної поверхні.
	 * Зовнішній індекс = лінія сліду (2 на поверхню: корінь + кінцівка); внутрішній = послідовні вузли.
	 */
	TArray<TArray<FTrailingVortexNode>> VortexWakeLines;

	void UpdateVortexWake();
	FVector GetInducedVelocity(const FVector& TargetPosCm) const;

	/** Акумулятор часу для дроселювання діагностичного логу (див. bLogFlightDebug). */
	float DebugLogAccumulator = 0.0f;
};
