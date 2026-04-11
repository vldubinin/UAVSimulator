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
	/** Налаштовує тік у групі TG_DuringPhysics для синхронізації з фізичним рушієм. */
	UFlightDynamicsComponent();

	/**
	 * Головний тік компонента: перевіряє наявність усіх кривих, застосовує тягу двигуна,
	 * аеродинамічні сили крила та хвоста для обох сторін (+1 і −1).
	 * @param DeltaTime — час кадру в секундах.
	 * @param TickType  — тип тіку (ігнорується).
	 * @param ThisTickFunction — функція тіку (передається до Super).
	 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	/**
	 * Ініціалізує Owner і Root, встановлює дилатацію часу,
	 * задає початкову швидкість якщо InitialSpeed > 0.
	 */
	virtual void BeginPlay() override;

	/** Застосовує силу тяги двигуна в напрямку вперед у точці EngineForcePointOffset. */
	void ApplyEngineForce();

	/**
	 * Збирає параметри крила та делегує обчислення до ApplyAerodynamicForceForSide.
	 * @param Side — +1 для лівого крила, −1 для правого (дзеркального).
	 */
	void ApplyWingForceForSide(int32 Side);

	/**
	 * Збирає параметри хвоста та делегує обчислення до ApplyAerodynamicForceForSide.
	 * Підйомна сила хвоста інвертована (bInvertLift = true).
	 * @param Side — +1 для лівої сторони, −1 для правої.
	 */
	void ApplyTailForceForSide(int32 Side);

private:
	/** Параметри для опису однієї сторони аеродинамічної поверхні. */
	struct FSurfaceForceParams
	{
		FVector ForcePointOffset;
		FVector LeadingEdgeOffset;
		FVector TrailingEdgeOffset;
		float   Area;
		UCurveFloat* CurveCl;
		UCurveFloat* CurveCd;
		float   LiftMultiplier;
		float   DragMultiplier;
		/** If true, lift direction is negated (used for tail surfaces). */
		bool    bInvertLift;
	};

	/**
	 * Єдина функція застосування аеродинамічних сил, спільна для крила та хвоста.
	 * Обчислює точку прикладання, напрямок хорди, кут атаки, підйомну силу та опір,
	 * застосовує їх до Root і за потреби малює відлагоджувальну візуалізацію.
	 * @param Side   — +1 або −1 (дзеркало по Y).
	 * @param Params — параметри поверхні (зміщення, площа, криві, множники).
	 * @param bLog   — true якщо потрібно вивести лог у консоль.
	 */
	void ApplyAerodynamicForceForSide(int32 Side, const FSurfaceForceParams& Params, bool bLog);

	/**
	 * Обчислює нормалізований вектор хорди від задньої до передньої кромки у світовому просторі.
	 * @param ForcePoint          — точка прикладання сили у світових координатах.
	 * @param LeadingEdgeOffset   — зміщення передньої кромки відносно ForcePoint (локальний простір).
	 * @param TrailingEdgeOffset  — зміщення задньої кромки відносно ForcePoint (локальний простір).
	 * @param Side                — знак симетрії по Y (+1 або −1).
	 * @return Нормалізований вектор хорди.
	 */
	FVector FindChordDirection(FVector ForcePoint, FVector LeadingEdgeOffset, FVector TrailingEdgeOffset, int32 Side);

	/**
	 * Повертає нормалізований вектор набігаючого потоку (протилежний до вектора швидкості актора).
	 * @param WorldOffset — не використовується (зарезервовано для майбутнього локального вітру).
	 * @return Нормалізований вектор або нульовий якщо ЛА стоїть на місці.
	 */
	FVector FindAirflowDirection(FVector WorldOffset);

	/**
	 * Обчислює напрямок підйомної сили як векторний добуток набігаючого потоку та правого вектора крила.
	 * @param WorldOffset      — не використовується.
	 * @param AirflowDirection — нормалізований вектор набігаючого потоку.
	 * @return Нормалізований вектор підйомної сили або нульовий вектор.
	 */
	FVector FindLiftDirection(FVector WorldOffset, FVector AirflowDirection);

	/**
	 * @return Поточна швидкість актора в м/с (конвертується з cm/s).
	 */
	float   GetSpeedInMetersPerSecond();

	/**
	 * Обчислює силу аеродинамічного опору в Ньютонах: D = CD * 0.5 * ρ * V² * S.
	 * @param CurveCd — крива CD від кута атаки.
	 * @param Area    — площа поверхні в м².
	 * @param AoA     — кут атаки в градусах.
	 * @return Сила опору в Ньютонах.
	 */
	float   CalculateDragInNewtons(UCurveFloat* CurveCd, float Area, float AoA);

	/**
	 * Обчислює підйомну силу в Ньютонах: L = CL * 0.5 * ρ * V² * S.
	 * @param CurveCl — крива CL від кута атаки.
	 * @param Area    — площа поверхні в м².
	 * @param AoA     — кут атаки в градусах.
	 * @return Підйомна сила в Ньютонах.
	 */
	float   CalculateLiftInNewtons(UCurveFloat* CurveCl, float Area, float AoA);

	/**
	 * Обчислює кут атаки через скалярний добуток хорди та вектора потоку.
	 * Повертає 0 якщо ЛА стоїть на місці.
	 * @param AirflowDirection  — нормалізований вектор набігаючого потоку.
	 * @param WingChordDirection — нормалізований вектор хорди.
	 * @return Кут атаки в градусах [0, 180].
	 */
	float   CalculateAoA(FVector AirflowDirection, FVector WingChordDirection);

	/**
	 * Виводить повідомлення на екран якщо DebugUILogs == true.
	 * @param Text — рядок повідомлення для відображення на екрані.
	 */
	void LogMsg(FString Text);

private:
	UPROPERTY()
	AActor* Owner;

	UPROPERTY()
	UPrimitiveComponent* Root;

	int32 TickNumber = 0;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Unreal", meta = (ToolTip = "Швидкість роботи симуляції. Значення '1' відповідає звичайній швидкості."))
		float DebugSimulatorSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Engine", meta = (ToolTip = "Максимальна сила тяги двигуна в Ньютонах"))
		float ThrustForce = 1350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Engine", meta = (ToolTip = "Зміщення точки прикладання сил двигуна відносно центру мас."))
		FVector EngineForcePointOffset = FVector(-40.0f, 0.0f, 0.0f);

	// ~~~ Вхідні дані керування ~~~
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Множник підйомної сили крила."))
		float LiftWingForseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Множник підйомної сили хвоста."))
		float LiftTailForseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення підйомної сили."))
		bool DisableLiftForse = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Множник сили опору крила."))
		float DragWingForseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Множник сили опору хвоста."))
		float DragTailForseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Debug", meta = (ToolTip = "Виключення супротиву повітря."))
		bool DisableDragForse = false;

	UPROPERTY(EditAnywhere, Category = "Aircraft | Debug", meta = (ToolTip = "Початкова швидкість у м/с."))
		float InitialSpeed = 0.0f;
};
