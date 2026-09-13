#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "Math/Box2D.h"

#include "BBoxDetectionComponent.generated.h"

/**
 * Виявляє актори сцени за допомогою лідар-подібної розгортки променів від
 * SceneCaptureComponent2D власника, проєктує OBB кожного актора у 2D-простір
 * екрану і публікує отримані обмежувальні рамки як JSON на топіку "bbox".
 *
 * SceneActors збираються один раз на першому тіку; їхні 2D-проєкції
 * перераховуються щотіку з урахуванням поточної трансформації камери.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UBBoxDetectionComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UBBoxDetectionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("bbox"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

public:
	/** Максимальна дальність трасування променів у см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox", meta = (ClampMin = 1.0f))
	float Range = 5000.0f;

	/** Кількість променів, рівномірно розподілених по повному горизонтальному охопленню 360°. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox", meta = (ClampMin = 1))
	int32 HorizontalRays = 360;

	/** Кількість рівномірно розташованих вертикальних шарів сканування. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox", meta = (ClampMin = 1))
	int32 VerticalLayers = 16;

	/** Скільки повних сканувань виконується за секунду (зарезервовано для майбутнього обмеження частоти). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox", meta = (ClampMin = 0.1f, ClampMax = 100.0f))
	float ScanRate = 10.0f;

	/** Канал колізій, що використовується для трасування променів. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BBox")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

private:
	void CollectSceneActors();
	FBox2D ProjectActorToScreen(AActor* Actor) const;
	FString SerializeBBoxes(const TMap<FString, FBox2D>& BBoxMap) const;

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent = nullptr;

	float VerticalFOVDeg = 0.0f;

	TArray<AActor*> SceneActors;

	// Останній серіалізований кадр — записується і читається лише в ігровому потоці.
	TArray<uint8> LatestPayload;
	double        LatestTimestamp = 0.0;
	bool          bHasFrame = false;
};
