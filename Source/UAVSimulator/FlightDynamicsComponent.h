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

	void ApplyEngineForce();
	void ApplyWingForceForSide(int32 Side);
	void ApplyTailForceForSide(int32 Side);


private:
	FVector FindChordDirection(FVector ForcePoint, FVector LeadingEdgeOffset, FVector TrailingEdgeOffset, int32 Side);
	FVector FindAirflowDirection(FVector WorldOffset);
	FVector FindLiftDirection(FVector WorldOffset, FVector AirflowDirection);
	float GetSpeedInMetersPerSecond();
	float CalculateDragInNewtons(UCurveFloat* CurveCd, float Area, float AoA);
	float CalculateLiftInNewtons(UCurveFloat* CurveCl, float Area, float AoA);
	float CalculateAoA(FVector AirflowDirection, FVector WingChordDirection);
	float NewtonsToKiloCentimeter(float Newtons);


	void LogMsg(FString Text);
	void DrawVectorAsArrow(const UWorld* World, FVector StartLocation, FVector Direction, FColor Color = FColor::Red, float Multiplier = 1.0f,  float ArrowSize = 1.0f, float Thickness = 0.5f, float Duration = -1.0f);

private:
	// Кешовані посилання для оптимізації
	UPROPERTY()
		AActor* Owner;

	UPROPERTY()
		UPrimitiveComponent* Root;

	int32 TickNumber = 0;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Unreal", meta = (ToolTip = "Швидкість роботи симуляції. Значення '1' відповідає звичайній швидкості."))
		float DebugSimulatorSpeed = 1.0f;

	// ДОДАНО: Параметр для тяги двигуна
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Engine", meta = (ToolTip = "Максимальна сила тяги двигуна в Ньютонах"))
		float ThrustForce = 1350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Engine", meta = (ToolTip = "Зміщення точки прикладання сил двигуна відносно центру мас."))
		FVector EngineForcePointOffset = FVector(-40.0f, 0.0f, 0.0f);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Aerodynamics", meta = (ToolTip = "Щільність повітря в кг/м³"))
		float AirDensity = 1.225f;


	// ~~~ РОЗТАШУВАННЯ ПОВЕРХОНЬ КРИЛ ~~~
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Зміщення аеродинамічного центру крила відносно центру мас."))
		FVector WingForcePointOffset = FVector(-2.0f, 20.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Локальне зміщення передньої кромки хорди крила відносно центру мас."))
		FVector WingLeadingEdgeOffset = FVector(6.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Локальне зміщення задньої кромки хорди крила відносно центру мас."))
		FVector WingTrailingEdgeOffset = FVector(-10.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Загальна площа крила в м²"))
		float WingArea = 0.14f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Зміщення аеродинамічного центру хвоста відносно центру мас."))
		FVector TailForcePointOffset = FVector(-35.0f, 10.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Локальне зміщення передньої кромки хорди хвоста відносно центру мас."))
		FVector TailLeadingEdgeOffset = FVector(4.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Локальне зміщення задньої кромки хорди хвоста відносно центру мас."))
		FVector TailTrailingEdgeOffset = FVector(-5.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Surfaces", meta = (ToolTip = "Загальна площа хвоста в м² (приблизно 15-20% від площі крила)"))
		float TailArea = 0.025f;

	// ~~~ НАЛАГОДЖЕННЯ ~~~

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення логів в консолі."))
		bool DebugConsoleLogs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення логів на екрані."))
		bool DebugUILogs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення візуальних маркерів."))
		bool bDrawDebugMarkers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Розмір маркерів."))
		float bDebugMarkersSize = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Розмір векторів сили."))
		float bDebugForceVectorSize = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення векторів для крил."))
		bool DebugDrawWingsInfo = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення векторів для хвоста."))
		bool DebugDrawTailInfo = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення логів в консолі."))
		float LiftWingForseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення логів в консолі."))
		float LiftTailForseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення підйомної сили."))
		bool DisableLiftForse = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення логів в консолі."))
		float DragWingForseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення відображення логів в консолі."))
		float DragTailForseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення супротиву повітря."))
		bool DisableDragForse = false;
	
	UPROPERTY(EditAnywhere, Category = "Aircraft | Debug", meta = (ToolTip = "Початкова швидкість у м/с."))
		float InitialSpeed = 0.0f;
};