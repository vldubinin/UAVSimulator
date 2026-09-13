#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"

#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>
#include "PostOpenCVHeaders.h"

#include "DroneDatasetGeneratorActor.generated.h"

/**
 * Інструментарний actor для редактора. Розмістіть на будь-якому рівні, призначте DroneBlueprintClass,
 * налаштуйте параметри сферичного обходу та OutputJsonPath, після чого натисніть кнопку
 * "Generate Dataset" у панелі Details.
 *
 * Стратегія маски: використовує PRM_UseShowOnlyList, щоб рендерився лише заспавнений дрон
 * на гарантовано чорному фоні. Немає залежності від post-process матеріалу.
 * Якщо призначено MaskPostProcessMaterial, він додатково інжектується поверх (опційно).
 *
 * Для кожної пари (азимут, кут підвищення) actor:
 *   1. Спавнить дрон, розташовує камеру захоплення на орбітальній сфері.
 *   2. Тимчасово перемикає захоплення в режим show-only-drone, викликає
 *      CaptureScene(), робить flush через ReadPixels().
 *   3. Витягує контур за допомогою OpenCV (поріг Otsu + morph-close + approxPolyDP).
 *   4. Накопичує всі кадри у JSON-файл і опційно зберігає debug PNG.
 */
UCLASS(Blueprintable)
class UAVSIMULATOR_API ADroneDatasetGeneratorActor : public AActor
{
	GENERATED_BODY()

public:
	ADroneDatasetGeneratorActor();

	// ── Налаштування ──────────────────────────────────────────────────────────
	/** Blueprint дрона для рендерингу. Перетягніть його сюди з Content Browser. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Setup")
	TSubclassOf<AActor> DroneBlueprintClass;

	/** Опційно. Post-process матеріал, що інжектується під час захоплення (наприклад,
	 *  toon-shader на основі custom stencil). Залиште null — підхід show-only-mask працює й без нього. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Setup")
	UMaterialInterface* MaskPostProcessMaterial = nullptr;

	// ── Камера ────────────────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera",
		meta = (ClampMin = 10.0, ClampMax = 150.0))
	float CameraFOV = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 32))
	int32 RenderWidth = 640;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 32))
	int32 RenderHeight = 480;

	// ── Обхід ─────────────────────────────────────────────────────────────────
	/** Радіус орбіти камери навколо origin actor'а дрона, в см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 50.0))
	float OrbitRadius = 2000.0f;

	/** Крок обходу по азимуту в градусах. 0° починається вздовж осі +X, зростає проти годинникової стрілки. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep",
		meta = (ClampMin = 1.0, ClampMax = 180.0))
	float AzimuthStep = 45.0f;

	/** Крок кілець за кутом підвищення в градусах. Кільця розташовуються на −90+step, −90+2·step, …, 90−step.
	 *  Північний (El=90) та південний (El=−90) полюси завжди включаються як окремі кадри. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep",
		meta = (ClampMin = 1.0, ClampMax = 89.0))
	float ElevationStep = 30.0f;

	// ── OpenCV ────────────────────────────────────────────────────────────────

	/** Розмір ядра гаусового розмиття, застосованого перед порогуванням (пікселі, має бути непарним).
	 *  Згладжує зазубрені краї силуету; більші значення дають м'якшу межу.
	 *  Встановіть 1, щоб вимкнути розмиття. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Processing",
		meta = (ClampMin = 1, ClampMax = 31))
	int32 SilhouetteBlurSize = 7;

	/** Розмір ядра морфологічного closing (пікселі).
	 *  Об'єднує тонкі виступи — стійки шасі, антени, розпірки крил — з
	 *  основним тілом силуету. Збільшуйте, якщо дрібні деталі досі відокремлюються. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Processing",
		meta = (ClampMin = 1, ClampMax = 99))
	int32 FillGapsSize = 15;

	/** Розмір ядра морфологічного opening (пікселі).
	 *  Стирає ізольовані плями, що залишилися після проходу fill-gaps.
	 *  Тримайте менше за FillGapsSize, щоб не роз'їдати сам дрон. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Processing",
		meta = (ClampMin = 1, ClampMax = 99))
	int32 RemoveIslandsSize = 9;

	/** Допуск апроксимації полігона: частка периметра контуру.
	 *  Більше значення → менше вершин, плавніша форма. 0.02 — хороша відправна точка. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Processing",
		meta = (ClampMin = 0.001, ClampMax = 0.5))
	float PolygonSmoothness = 0.02f;

	// ── Вивід ─────────────────────────────────────────────────────────────────
	/** Абсолютний шлях до вихідного JSON-файлу, наприклад C:/Datasets/drone.json */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	FString OutputJsonPath;

	/** Якщо true, зберігає по одному PNG на кадр з накладеним на маску полігоном. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	bool bSaveDebugImages = true;

	/** Директорія для debug-зображень. Залиште порожнім, щоб автоматично створити підпапку поряд з
	 *  OutputJsonPath з назвою <json-basename>_frames/. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output",
		meta = (EditCondition = "bSaveDebugImages"))
	FString OutputImageDir;

	/** Натисніть цю кнопку, щоб почати генерацію датасету. Блокує редактор до завершення. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void GenerateDataset();

private:
	struct FFrameData
	{
		float             Azimuth;
		float             Elevation;
		TArray<FVector2D> Polygon;
	};

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComp = nullptr;

	void              PlaceCameraAt(const FVector& Target, float AzimuthDeg, float ElevationDeg);
	bool              CaptureMask(AActor* DroneActor, UTextureRenderTarget2D* RT, TArray<FColor>& OutPixels);
	TArray<FVector2D> ExtractPolygon(const TArray<FColor>& Pixels) const;
	void              SaveDebugImage(const TArray<FColor>& Pixels, const TArray<FVector2D>& Polygon,
	                                 float AzimuthDeg, float ElevationDeg,
	                                 const FString& ImageDir, int FrameId) const;
	FString           BuildJson(const FString& ModelName, const TArray<FFrameData>& Frames) const;
};
