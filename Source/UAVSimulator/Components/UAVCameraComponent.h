#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"

#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include "PostOpenCVHeaders.h"

#include "UAVCameraComponent.generated.h"

class ACesiumCameraManager;
class UMaterialInstanceDynamic;
class AEWZoneActor;

/**
 * Керує бортовою камерою: RGB-захопленням, обробкою OpenCV, опційним захопленням маски
 * сегментації та стабільними в межах тіку JPEG-корисними навантаженнями для споживачів нижче
 * за течією.
 *
 * Викликайте GetRGBFrame() / GetMaskFrame() в ігровому потоці, щоб отримати останнє
 * JPEG-закодоване навантаження для поточного тіку. Кілька викликів у межах одного тіку
 * завжди повертають той самий знімок.
 *
 * JPEG-кодування кожного потоку виконується у виділеному фоновому потоці.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UUAVCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUAVCameraComponent();

	void ProcessFrame();
	void SetCameraProcessingEnabled(bool bEnable);

	/**
	 * Повертає стабільне в межах тіку JPEG-закодоване RGB-навантаження.
	 * Кілька викликів у межах одного тіку повертають той самий знімок.
	 * Має викликатися в ігровому потоці.
	 */
	bool GetRGBFrame(TArray<uint8>& OutPayload, double& OutTimestamp) const;

	/**
	 * Повертає стабільне в межах тіку JPEG-закодоване навантаження маски сегментації.
	 * Видає дані лише коли встановлено MaskPostProcessMaterial.
	 * Має викликатися в ігровому потоці.
	 */
	bool GetMaskFrame(TArray<uint8>& OutPayload, double& OutTimestamp) const;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Оброблена вихідна текстура — прив'язуйте у віджеті або матеріалі. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	UTexture2D* OutputTexture;

	/** Горизонтальний FOV камери захоплення, у градусах. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	float HorizontalFOVDeg = 0.0f;

	/** Вертикальний FOV камери захоплення, у градусах. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	float VerticalFOVDeg = 0.0f;

	/** Якість JPEG для закодованих кадрів (1–100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 100))
	int32 JpegQuality = 80;

	/** Максимальна кількість кодувань JPEG за секунду для обох потоків. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 120))
	int32 MaxEncodeFPS = 30;

	/** Матеріал пост-процесу, що перетворює Custom Stencil на чорно-білу маску.
	 *  Коли встановлено, захоплення маски виконується разом з RGB щотіку. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segmentation")
	UMaterialInterface* MaskPostProcessMaterial = nullptr;

private:
	void UploadToTexture();
	void CaptureMask();
	void RGBEncoderLoop();
	void MaskEncoderLoop();
	void ComputeFOV(float HFovDeg);
	void LogCameraIntrinsics() const;

	// ── Перешкоди РЕБ (Electronic Warfare) ────────────────────────────────────
	/** Перетворює всі матеріали пост-процесу, вручну додані в PostProcessMaterials
	 *  CaptureComponent (напр. M_EW_Interference), на MID для щотікового керування параметрами. */
	void InitEWInterferenceMIDs();
	/** Виставляє Interference_Intensity/Distortion_Strength/Noise_Intensity на максимум
	 *  AEWZoneActor::GetInterferenceIntensity() серед усіх зон РЕБ (найближча/найсильніша
	 *  домінує). Перешкоди активні автоматично, якщо є хоч одна зона в радіусі дії — окремого
	 *  глобального вмикача немає. */
	void UpdateEWInterference();

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> EWInterferenceMIDs;

	/** Усі зони РЕБ на сцені (синхронізується з UUAVSimulationSubsystem::EWZones) — кожна
	 *  сама рахує свою інтенсивність у AEWZoneActor::GetInterferenceIntensity(). */
	TArray<TWeakObjectPtr<AEWZoneActor>> EWZones;

	FDelegateHandle EWSettingsChangedHandle;

	// ── Реєстрація камери захоплення сцени Cesium ─────────────────────────────
	/** Знаходить або спавнить менеджер камер Cesium для цього (ігрового) світу. */
	void ResolveCesiumCameraManager();
	/** Щокадрове додавання-або-оновлення нашого FCesiumCamera, щоб Cesium уточнював тайли для цього захоплення. */
	void SyncCesiumSceneCaptureCamera();
	/** Видаляє наш FCesiumCamera при вимкненні / знищенні. */
	void UnregisterCesiumSceneCaptureCamera();

	/** Менеджер камер Cesium для цього світу (розв'язується лінькаво; самообнуляється). */
	TWeakObjectPtr<ACesiumCameraManager> CesiumCameraManager;

	/** Стабільний id з ACesiumCameraManager::AddCamera; INDEX_NONE, доки не зареєстровано. */
	int32 CesiumCameraId = INDEX_NONE;

	/** True в проміжку між успішним AddCamera і відповідним RemoveCamera. */
	bool bCesiumCameraRegistered = false;

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent;

	UPROPERTY()
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY()
	UTextureRenderTarget2D* MaskRenderTarget;

	FUpdateTextureRegion2D* UpdateRegion = nullptr;
	cv::Mat ProcessedFrameBuffer;
	bool bIsProcessingEnabled = false;

	static constexpr int32 CVWidth  = 640;
	static constexpr int32 CVHeight = 480;

	// ── Вхід кодування RGB (ігровий потік → потік кодувальника) ───────────────────────
	TArray<uint8>    PendingRGBBGRA;
	double           PendingRGBTimestamp  = 0.0;
	bool             bHasPendingRGBFrame  = false;
	FCriticalSection RGBFrameMutex;

	// ── Вихід кодування RGB (потік кодувальника → ігровий потік) ─────────────────
	TArray<uint8>    LatestRGBPayload;
	double           LatestRGBTimestamp   = 0.0;
	bool             bHasLatestRGBFrame   = false;
	FCriticalSection LatestRGBMutex;

	// ── Потік кодувальника RGB ────────────────────────────────────────────────────
	FEvent*          RGBFrameReadyEvent   = nullptr;
	FThreadSafeBool  bRGBEncoderRunning;
	FRunnable*       RGBEncoderRunnable   = nullptr;
	FRunnableThread* RGBEncoderThread     = nullptr;

	double MinEncodeInterval  = 1.0 / 30.0;
	double LastRGBEncodeTime  = 0.0;

	// ── Стабільний у межах тіку кеш RGB (лише ігровий потік, записується раз на тік) ───────
	TArray<uint8> TickRGBPayload;
	double        TickRGBTimestamp  = 0.0;
	bool          bHasTickRGBFrame  = false;

	// ── Вхід кодування маски (ігровий потік → потік кодувальника) ─────────────────
	TArray<uint8>    PendingMaskBGRA;
	double           PendingMaskTimestamp  = 0.0;
	bool             bHasPendingMaskFrame  = false;
	FCriticalSection MaskFrameMutex;

	// ── Вихід кодування маски (потік кодувальника → ігровий потік) ────────────────
	TArray<uint8>    LatestMaskPayload;
	double           LatestMaskTimestamp   = 0.0;
	bool             bHasLatestMaskFrame   = false;
	FCriticalSection LatestMaskMutex;

	// ── Потік кодувальника маски ───────────────────────────────────────────────────
	FEvent*          MaskFrameReadyEvent   = nullptr;
	FThreadSafeBool  bMaskEncoderRunning;
	FRunnable*       MaskEncoderRunnable   = nullptr;
	FRunnableThread* MaskEncoderThread     = nullptr;

	double LastMaskEncodeTime = 0.0;

	// ── Стабільний у межах тіку кеш маски (лише ігровий потік, записується раз на тік) ──────
	TArray<uint8> TickMaskPayload;
	double        TickMaskTimestamp  = 0.0;
	bool          bHasTickMaskFrame  = false;
};
