#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/SceneComponent/KeyPoint/KeyPointComponent.h"

#include "KeyPointDetectionComponent.generated.h"

/**
 * Розміщується на акторі-СПОСТЕРІГАЧІ (тому самому, що й CameraFrameComponent та SensorBusComponent).
 * Використовує те саме лідар-подібне променеве розгортання, що й BBoxDetectionComponent, для
 * пошуку найближчих акторів, потім проєктує кожен UKeyPointComponent, знайдений на цих акторах,
 * на SceneCaptureComponent2D спостерігача та публікує результати як JSON у топіку "keypoints".
 *
 * Екземпляри UKeyPointComponent мають розміщуватися на блупринті ЦІЛЬОВОГО дрона — а НЕ на
 * спостерігачі. Спостерігач знаходить їх автоматично через променеве розгортання.
 *
 * Формат вихідного JSON (один payload на такт шини):
 * {
 *   "Cessna_172_C_0": [
 *     { "id": "nose",     "x": 320.5, "y": 240.1, "visible": true  },
 *     { "id": "left_wing","x":  10.2, "y": 198.3, "visible": false }
 *   ],
 *   "Cessna_172_C_1": [ ... ]
 * }
 * "visible" дорівнює false, коли точка знаходиться за камерою або за межами кадру.
 * x/y завжди присутні, щоб споживачі могли визначити оклюзію без зміни довжини масиву.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UKeyPointDetectionComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UKeyPointDetectionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("keypoints"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

public:
	/** Максимальна дальність променів для пошуку цільових акторів (см). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint", meta = (ClampMin = 1.0f))
	float Range = 5000.0f;

	/** Кількість горизонтальних променів у розгортанні пошуку. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint", meta = (ClampMin = 1))
	int32 HorizontalRays = 360;

	/** Кількість вертикальних шарів у розгортанні пошуку. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint", meta = (ClampMin = 1))
	int32 VerticalLayers = 16;

	/** Канал колізії, що використовується для трасування променів. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KeyPoint")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

private:
	struct FKeyPoint2D
	{
		FString   ID;
		FVector2D ScreenPosition;
		bool      bVisible;
	};

	/** Проєктує одну точку зі світового простору на render target.
	 *  Повертає true, якщо точка потрапляє в межі зображення. */
	bool ProjectWorldToScreen(const FVector& WorldPos, FVector2D& OutScreenPos) const;

	/** Серіалізує ключові точки всіх акторів в один JSON-об'єкт, ключем якого є ім'я актора. */
	FString SerializeAllKeyPoints(const TMap<FString, TArray<FKeyPoint2D>>& PerActorKeyPoints) const;

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent = nullptr;

	float VerticalFOVDeg = 0.0f;
	int32 SizeX          = 0;
	int32 SizeY          = 0;

	// Останній серіалізований кадр — записується й читається лише в ігровому потоці.
	TArray<uint8> LatestPayload;
	double        LatestTimestamp = 0.0;
	bool          bHasFrame       = false;
};
