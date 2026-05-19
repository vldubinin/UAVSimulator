#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"

#include "SegmentationMaskCameraComponent.generated.h"

/**
 * Generates a B&W segmentation mask (Custom Stencil) each tick by temporarily
 * applying MaskPostProcessMaterial to the owner's shared USceneCaptureComponent2D,
 * capturing to a private render target, and publishing JPEG-encoded frames on the
 * "segmentation_mask" sensor topic.
 *
 * Does not create a new USceneCaptureComponent2D — borrows and immediately restores
 * the existing one so UUAVCameraComponent's regular RGB capture is unaffected.
 * Tick-prerequisite on UUAVCameraComponent ensures this runs after RGB capture each frame.
 *
 * JPEG encoding runs on a dedicated background thread. GetLatestFrame() is safe to
 * call from the game thread at any time under its own mutex.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API USegmentationMaskCameraComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	USegmentationMaskCameraComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("segmentation_mask"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Post-process material that converts the Custom Stencil buffer to a B&W mask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segmentation")
	UMaterialInterface* MaskPostProcessMaterial = nullptr;

	/** JPEG quality for encoded mask frames (1–100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 100))
	int32 JpegQuality = 80;

	/** Maximum JPEG encodes per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 120))
	int32 MaxEncodeFPS = 30;

private:
	void EncoderLoop();

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent = nullptr;

	UPROPERTY()
	UTextureRenderTarget2D* MaskRenderTarget = nullptr;

	static constexpr int32 MaskWidth  = 640;
	static constexpr int32 MaskHeight = 480;

	// Set to true after the first CaptureScene() so we don't read an uninitialised RT.
	bool bHasPendingCapture = false;

	// ── Encode input (game thread → encoder thread) ───────────────────────────
	TArray<uint8>    PendingBGRA;
	double           PendingTimestamp = 0.0;
	bool             bHasPendingFrame = false;
	FCriticalSection FrameMutex;

	// ── Encode output (encoder thread → game thread) ──────────────────────────
	TArray<uint8>    LatestEncodedPayload;
	double           LatestEncodedTimestamp = 0.0;
	bool             bHasLatestFrame = false;
	FCriticalSection LatestFrameMutex;

	// ── Encoder thread ────────────────────────────────────────────────────────
	FEvent*          FrameReadyEvent = nullptr;
	FThreadSafeBool  bEncoderRunning;
	FRunnable*       EncoderRunnable = nullptr;
	FRunnableThread* EncoderThread   = nullptr;

	double MinEncodeInterval = 1.0 / 30.0;
	double LastEncodeTime    = 0.0;
};
