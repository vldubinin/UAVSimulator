#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Util/SensorUtilityLibrary.h"
#include "LidarComponent.generated.h"

/**
 * Датчик LiDAR, встановлений у настроюваній позиції на літаку.
 * Кожне сканування випускає промені за сферичним патерном і заповнює LatestScanResults:
 *   ім'я актора -> відстань до найближчого влучання (в см Unreal).
 *
 * Реалізує IUAVSensorInterface — SensorBusComponent викликає GetLatestFrame()
 * на кожному такті шини, щоб отримати результати сканування як JSON-payload.
 *
 * Як USceneComponent, датчик можна розмістити будь-де в ієрархії компонентів актора,
 * і саме його трансформація використовується як точка початку сканування.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API ULidarComponent : public USceneComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	ULidarComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("lidar"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/**
	 * Виконує повне сканування негайно в ігровому потоці, оновлює LatestScanResults
	 * і повертає посилання на нього. Автоматично викликається з TickComponent із частотою
	 * ScanRate Гц; також може бути викликаний напряму з Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar")
	const TMap<FString, float>& Scan();

	/** Останні результати сканування: ім'я актора -> відстань до найближчого влучання в см Unreal. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lidar")
	TMap<FString, float> LatestScanResults;

	// ── Параметри сканування ───────────────────────────────────────────────────────

	/** Максимальна дальність виявлення в см Unreal (за замовчуванням 5000 см = 50 м). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 1.0f))
	float Range = 5000.0f;

	/** Кількість променів, рівномірно розподілених по повному горизонтальному розгортанню 360°. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 1))
	int32 HorizontalRays = 360;

	/** Кількість рівномірно розподілених вертикальних шарів сканування. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 1))
	int32 VerticalLayers = 16;

	/** Повне вертикальне поле зору в градусах, симетричне відносно горизонтальної площини. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 1.0f, ClampMax = 180.0f))
	float VerticalFOVDeg = 30.0f;

	/** Скільки повних сканувань виконується за секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = 0.1f, ClampMax = 100.0f))
	float ScanRate = 10.0f;

	/** Канал колізії, що використовується для трасування променів. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

private:
	float  GetVerticalAngle(int32 V) const;

	float  ScanAccumulator      = 0.0f;
	double LatestScanTimestamp  = 0.0;   // ігровий час, коли Scan() востаннє завершився
};
