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
 * and output texture. Implements IUAVSensorInterface — subscribe to OnSensorDataReady
 * to receive JPEG-encoded frames. Add this component to the aircraft pawn; it will find
 * the first USceneCaptureComponent2D on the owner at BeginPlay.
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
	virtual FOnSensorDataReady& GetOnSensorDataReady() override { return OnSensorDataReady; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Processed output texture — bind in a widget or material. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	UTexture2D* OutputTexture;

	/** JPEG quality for frames sent over the sensor bus (1–100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 100))
	int32 JpegQuality = 80;

	/** Maximum frames per second forwarded to the sensor bus. Excess frames are dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 120))
	int32 MaxStreamFPS = 30;

	/** Fires on the game thread with a JPEG-encoded FSensorFrame after each encoded frame. */
	FOnSensorDataReady OnSensorDataReady;

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

	// ── JPEG encoding thread ──────────────────────────────────────────────────
	TArray<uint8>    PendingBGRA;
	bool             bHasPendingFrame = false;
	FCriticalSection FrameMutex;

	FEvent*          FrameReadyEvent = nullptr;
	FThreadSafeBool  bEncoderRunning;
	FRunnable*       EncoderRunnable = nullptr;
	FRunnableThread* EncoderThread   = nullptr;

	double MinSendInterval   = 1.0 / 30.0;
	double LastFrameSentTime = 0.0;
};
