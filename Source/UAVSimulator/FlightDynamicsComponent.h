#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "FlightDynamicsComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UFlightDynamicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightDynamicsComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

	void ApplyWingForceForSide(int32 Side);
	void ApplyTailForceForSide(int32 Side);


private:
	FRotator GetWingChordConfiguration(int32 Side);
	FRotator GetTailChordConfiguration(int32 Side);

	FVector FindChordDirection(FVector WorldOffset, FRotator TiltRotator, bool Draw);
	FVector FindAirflowDirection(FVector WorldOffset, bool Draw);
	FVector FindLiftDirection(FVector WorldOffset, FVector AirflowDirection, bool Draw);
	float GetSpeedInMetersPerSecond();
	float CalculateDragInNewtons(UCurveFloat* CurveCd, float Area, float AoA);
	float CalculateLiftInNewtons(UCurveFloat* CurveCl, float Area, float AoA);
	float CalculateAoA(FVector AirflowDirection, FVector WingChordDirection);
	float NewtonsToKiloCentimeter(float Newtons);


	void LogMsg(FString Text);

	float CalculateAoASimple(FVector AirflowDirection, FVector WingChordDirection);

private:
	// Кешовані посилання для оптимізації
	UPROPERTY()
		AActor* Owner;

	UPROPERTY()
		UPrimitiveComponent* Root;

	float LastLogTimestamp = 0.0f;
	FVector PrevLocation = FVector::ZeroVector;
	float PrevTime = 0.0f;
	FVector PrevVelocity = FVector::ZeroVector;

public:


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Unreal", meta = (ToolTip = "Вмикає прогрів Unreal Engine перед початком симуляції."))
		bool WarmUpEngine = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Unreal", meta = (ToolTip = "Швидкість роботи симуляції. Значення '1' відповідає звичайній швидкості."))
		float DebugSimulatorSpeed = 1.0f;

	// ДОДАНО: Параметр для тяги двигуна
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Engine", meta = (ToolTip = "Максимальна сила тяги двигуна в Ньютонах"))
		float MaxThrustForce = 20.0f;
	// ~~~ Вхідні дані керування (поки не використовуються) ~~~
	UPROPERTY(BlueprintReadWrite, Category = "Aircraft | Control Inputs", meta = (ToolTip = "Газ [0, 1]"))
		float ThrottleInput = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Aircraft | Control Inputs", meta = (ToolTip = "Тангаж [-1, 1]"))
		float PitchInput = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Aircraft | Control Inputs", meta = (ToolTip = "Рискання [-1, 1]"))
		float YawInput = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Aircraft | Control Inputs", meta = (ToolTip = "Крен [-1, 1]"))
		float RollInput = 0.0f;

	// ~~~ Аеродинамічні параметри ~~~
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Aerodynamics", meta = (ToolTip = "Крива коефіцієнта підйомної сили (CL)"))
		UCurveFloat* WingCurveCl;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Aerodynamics", meta = (ToolTip = "Крива коефіцієнта опору (CD)"))
		UCurveFloat* WingCurveCd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Aerodynamics", meta = (ToolTip = "Крива коефіцієнта підйомної сили (CL) для хвоста"))
		UCurveFloat* TailCurveCl;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Aerodynamics", meta = (ToolTip = "Крива коефіцієнта опору (CD) для хвоста"))
		UCurveFloat* TailCurveCd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Aerodynamics", meta = (ToolTip = "Загальна площа крила в м²"))
		float WingArea = 0.14f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Aerodynamics", meta = (ToolTip = "Загальна площа хвоста в м² (приблизно 15-20% від площі крила)"))
		float TailArea = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Aerodynamics", meta = (ToolTip = "Щільність повітря в кг/м³"))
		float AirDensity = 1.225f;

	// ~~~ РОЗТАШУВАННЯ ПОВЕРХОНЬ КРИЛ ~~~
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Зміщення аеродинамічного центру крила відносно центру мас."))
		FVector WingForcePointOffset = FVector(-2.0f, 20.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Зміщення хорди крила відносно локальної осі Y."))
		float WingChordYAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Зміщення хорди крила відносно локальної осі Z."))
		float WingChordZAngle;


	// ~~~ РОЗТАШУВАННЯ ПОВЕРХОНЬ ХВОСТА ~~~
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Зміщення аеродинамічного центру хвоста відносно центру мас."))
		FVector TailForcePointOffset = FVector(-35.0f, 10.0f, 5.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Зміщення хорди хвоста відносно локальної осі Y."))
		float TailChordYAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Зміщення хорди хвоста відносно локальної осі Z."))
		float TailChordZAngle;

	// ~~~ НАЛАГОДЖЕННЯ ~~~
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug")
		bool bDrawDebugMarkers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення векторів для крил."))
		bool DebugDrawWingsInfo = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення векторів для хвоста."))
		bool DebugDrawTailInfo = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Застосовувати сили у локальних координатах."))
		bool DebugIsLocalForce = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Застосувати силу."))
		FVector DebugForce = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Масштаб для візуалізації векторів сил. Збільште, якщо лінії занадто короткі."))
		float DebugForceScale = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення крила з розрахунків."))
		bool DebugDisableWings = false;	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення підйомної сили (Lift) для крил з розрахунків."))
		bool DebugDisableLiftForceWings = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення супротиву повітря (Drag) для крил з розрахунків."))
		bool DebugDisableDragForceWings = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення хвоста з розрахунків."))
		bool DebugDisableTail = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення підйомної сили (Lift) для хвоста з розрахунків."))
		bool DebugDisableLiftForceTail = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення супротиву повітря (Drag) для хвоста з розрахунків."))
		bool DebugDisableDragForceTail = false;

};