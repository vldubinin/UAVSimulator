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

/**
 * Manages the onboard camera: RGB capture, OpenCV processing, optional segmentation
 * mask capture, and per-tick stable JPEG payloads for downstream consumers.
 *
 * Call GetRGBFrame() / GetMaskFrame() on the game thread to obtain the latest
 * JPEG-encoded payload for the current tick. Multiple calls within the same tick
 * always return the same snapshot.
 *
 * JPEG encoding for each stream runs on a dedicated background thread.
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
	 * Returns the tick-stable JPEG-encoded RGB payload.
	 * Multiple calls within the same tick return the same snapshot.
	 * Must be called on the game thread.
	 */
	bool GetRGBFrame(TArray<uint8>& OutPayload, double& OutTimestamp) const;

	/**
	 * Returns the tick-stable JPEG-encoded segmentation mask payload.
	 * Only produces data when MaskPostProcessMaterial is set.
	 * Must be called on the game thread.
	 */
	bool GetMaskFrame(TArray<uint8>& OutPayload, double& OutTimestamp) const;

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Processed output texture — bind in a widget or material. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	UTexture2D* OutputTexture;

	/** Horizontal FOV of the capture camera in degrees. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	float HorizontalFOVDeg = 0.0f;

	/** Vertical FOV of the capture camera in degrees. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	float VerticalFOVDeg = 0.0f;

	/** JPEG quality for encoded frames (1–100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 100))
	int32 JpegQuality = 80;

	/** Maximum JPEG encodes per second for both streams. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 120))
	int32 MaxEncodeFPS = 30;

	/** Post-process material that converts the Custom Stencil to a B&W mask.
	 *  When set, mask capture runs alongside RGB each tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segmentation")
	UMaterialInterface* MaskPostProcessMaterial = nullptr;

private:
	void UploadToTexture();
	void CaptureMask();
	void RGBEncoderLoop();
	void MaskEncoderLoop();
	void ComputeFOV(float HFovDeg);

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

	// ── RGB encode input (game thread → encoder thread) ───────────────────────
	TArray<uint8>    PendingRGBBGRA;
	double           PendingRGBTimestamp  = 0.0;
	bool             bHasPendingRGBFrame  = false;
	FCriticalSection RGBFrameMutex;

	// ── RGB encode output (encoder thread → game thread) ─────────────────────
	TArray<uint8>    LatestRGBPayload;
	double           LatestRGBTimestamp   = 0.0;
	bool             bHasLatestRGBFrame   = false;
	FCriticalSection LatestRGBMutex;

	// ── RGB encoder thread ────────────────────────────────────────────────────
	FEvent*          RGBFrameReadyEvent   = nullptr;
	FThreadSafeBool  bRGBEncoderRunning;
	FRunnable*       RGBEncoderRunnable   = nullptr;
	FRunnableThread* RGBEncoderThread     = nullptr;

	double MinEncodeInterval  = 1.0 / 30.0;
	double LastRGBEncodeTime  = 0.0;

	// ── Tick-stable RGB cache (game thread only, written once per tick) ───────
	TArray<uint8> TickRGBPayload;
	double        TickRGBTimestamp  = 0.0;
	bool          bHasTickRGBFrame  = false;

	// ── Mask capture state ────────────────────────────────────────────────────
	bool bHasPendingMaskCapture = false;

	// ── Mask encode input (game thread → encoder thread) ─────────────────────
	TArray<uint8>    PendingMaskBGRA;
	double           PendingMaskTimestamp  = 0.0;
	bool             bHasPendingMaskFrame  = false;
	FCriticalSection MaskFrameMutex;

	// ── Mask encode output (encoder thread → game thread) ────────────────────
	TArray<uint8>    LatestMaskPayload;
	double           LatestMaskTimestamp   = 0.0;
	bool             bHasLatestMaskFrame   = false;
	FCriticalSection LatestMaskMutex;

	// ── Mask encoder thread ───────────────────────────────────────────────────
	FEvent*          MaskFrameReadyEvent   = nullptr;
	FThreadSafeBool  bMaskEncoderRunning;
	FRunnable*       MaskEncoderRunnable   = nullptr;
	FRunnableThread* MaskEncoderThread     = nullptr;

	double LastMaskEncodeTime = 0.0;

	// ── Tick-stable mask cache (game thread only, written once per tick) ──────
	TArray<uint8> TickMaskPayload;
	double        TickMaskTimestamp  = 0.0;
	bool          bHasTickMaskFrame  = false;
};
