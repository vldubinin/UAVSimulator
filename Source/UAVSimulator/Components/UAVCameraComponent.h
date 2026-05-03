#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"

#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include "PostOpenCVHeaders.h"

#include "UAVCameraComponent.generated.h"

/**
 * Manages the onboard camera: render target, OpenCV image processing, JPEG encoding,
 * and output texture. Implements IUAVSensorInterface — SensorBusComponent calls
 * GetLatestFrame() each bus tick to retrieve the most recently encoded JPEG frame.
 *
 * JPEG encoding runs on a dedicated background thread so the game thread is never
 * stalled by compression. The latest encoded result is double-buffered and read
 * under a mutex by GetLatestFrame().
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UUAVCameraComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UUAVCameraComponent();

	void ProcessFrame();
	void SetCameraProcessingEnabled(bool bEnable);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("camera"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Processed output texture — bind in a widget or material. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	UTexture2D* OutputTexture;

	/** JPEG quality for encoded frames (1–100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 100))
	int32 JpegQuality = 80;

	/** Maximum JPEG encodes per second. Acts as a CPU budget cap; the bus reads
	 *  at its own rate independently. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 120))
	int32 MaxEncodeFPS = 30;

private:
	void UploadToTexture();
	void EncoderLoop();

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent;

	UPROPERTY()
	UTextureRenderTarget2D* RenderTarget;

	FUpdateTextureRegion2D* UpdateRegion = nullptr;
	cv::Mat ProcessedFrameBuffer;
	bool bIsProcessingEnabled = false;

	static constexpr int32 CVWidth  = 640;
	static constexpr int32 CVHeight = 480;

	// ── Encode input (game thread → encoder thread) ───────────────────────────
	TArray<uint8>    PendingBGRA;
	double           PendingTimestamp  = 0.0;   // world time of the captured frame
	bool             bHasPendingFrame  = false;
	FCriticalSection FrameMutex;                 // guards PendingBGRA / PendingTimestamp / bHasPendingFrame

	// ── Encode output (encoder thread → game thread) ──────────────────────────
	TArray<uint8>    LatestEncodedPayload;
	double           LatestEncodedTimestamp = 0.0;
	bool             bHasLatestFrame  = false;
	FCriticalSection LatestFrameMutex;           // guards the three fields above

	// ── Encoder thread ────────────────────────────────────────────────────────
	FEvent*          FrameReadyEvent = nullptr;
	FThreadSafeBool  bEncoderRunning;
	FRunnable*       EncoderRunnable = nullptr;
	FRunnableThread* EncoderThread   = nullptr;

	double MinEncodeInterval   = 1.0 / 30.0;
	double LastEncodeTime      = 0.0;
};
