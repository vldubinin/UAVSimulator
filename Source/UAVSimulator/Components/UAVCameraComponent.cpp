#include "UAVCameraComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"
#include "HAL/RunnableThread.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Minimal FRunnable wrapper
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	class FLambdaRunnable final : public FRunnable
	{
	public:
		explicit FLambdaRunnable(TUniqueFunction<void()> InBody)
			: Body(MoveTemp(InBody)) {}
		virtual uint32 Run() override { Body(); return 0; }
	private:
		TUniqueFunction<void()> Body;
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

UUAVCameraComponent::UUAVCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	CaptureComponent = nullptr;
	RenderTarget     = nullptr;
	MaskRenderTarget = nullptr;
	OutputTexture    = nullptr;
	ComputeFOV(90.0f);
}

void UUAVCameraComponent::OnRegister()
{
	Super::OnRegister();

	AActor* Owner = GetOwner();
	if (Owner)
	{
		if (USceneCaptureComponent2D* Comp = Owner->FindComponentByClass<USceneCaptureComponent2D>())
			ComputeFOV(Comp->FOVAngle);
	}
}

#if WITH_EDITOR
void UUAVCameraComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	AActor* Owner = GetOwner();
	if (Owner)
	{
		if (USceneCaptureComponent2D* Comp = Owner->FindComponentByClass<USceneCaptureComponent2D>())
			ComputeFOV(Comp->FOVAngle);
	}
}
#endif

void UUAVCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	CaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	if (!CaptureComponent)
	{
		UE_LOG(LogUAV, Error, TEXT("UAVCameraComponent: USceneCaptureComponent2D not found on %s."), *Owner->GetName());
		return;
	}

	ComputeFOV(CaptureComponent->FOVAngle);
	UE_LOG(LogUAV, Log, TEXT("UAVCameraComponent: Camera found on %s (HFOV=%.1f° VFOV=%.1f°)"),
		*Owner->GetName(), HorizontalFOVDeg, VerticalFOVDeg);

	// RGB render target
	RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitCustomFormat(CVWidth, CVHeight, PF_B8G8R8A8, false);
	RenderTarget->UpdateResourceImmediate(false);
	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// Output texture
	OutputTexture = UTexture2D::CreateTransient(CVWidth, CVHeight, PF_B8G8R8A8);
	OutputTexture->UpdateResource();
	if (FTexture2DMipMap* Mip = &OutputTexture->GetPlatformData()->Mips[0])
	{
		void* Data = Mip->BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memset(Data, 0, CVWidth * CVHeight * 4);
		Mip->BulkData.Unlock();
	}
	OutputTexture->UpdateResource();

	UpdateRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, CVWidth, CVHeight);

	// Mask render target — always allocated; capture is gated on MaskPostProcessMaterial
	MaskRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	MaskRenderTarget->InitCustomFormat(CVWidth, CVHeight, PF_B8G8R8A8, false);
	MaskRenderTarget->UpdateResourceImmediate(false);

	MinEncodeInterval  = 1.0 / FMath::Max(MaxEncodeFPS, 1);
	LastRGBEncodeTime  = -MinEncodeInterval;
	LastMaskEncodeTime = -MinEncodeInterval;
	PendingRGBBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);
	PendingMaskBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);

	// RGB encoder thread
	RGBFrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bRGBEncoderRunning = true;
	RGBEncoderRunnable = new FLambdaRunnable([this]() { RGBEncoderLoop(); });
	RGBEncoderThread   = FRunnableThread::Create(RGBEncoderRunnable, TEXT("UAV_RGBEncoder"), 0, TPri_BelowNormal);

	// Mask encoder thread
	MaskFrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bMaskEncoderRunning = true;
	MaskEncoderRunnable = new FLambdaRunnable([this]() { MaskEncoderLoop(); });
	MaskEncoderThread   = FRunnableThread::Create(MaskEncoderRunnable, TEXT("UAV_MaskEncoder"), 0, TPri_BelowNormal);
}

void UUAVCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RGBEncoderThread)
	{
		bRGBEncoderRunning = false;
		if (RGBFrameReadyEvent) RGBFrameReadyEvent->Trigger();
		RGBEncoderThread->WaitForCompletion();
		delete RGBEncoderThread;
		RGBEncoderThread = nullptr;
	}
	delete RGBEncoderRunnable;
	RGBEncoderRunnable = nullptr;

	if (MaskEncoderThread)
	{
		bMaskEncoderRunning = false;
		if (MaskFrameReadyEvent) MaskFrameReadyEvent->Trigger();
		MaskEncoderThread->WaitForCompletion();
		delete MaskEncoderThread;
		MaskEncoderThread = nullptr;
	}
	delete MaskEncoderRunnable;
	MaskEncoderRunnable = nullptr;

	if (RGBFrameReadyEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(RGBFrameReadyEvent);
		RGBFrameReadyEvent = nullptr;
	}
	if (MaskFrameReadyEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(MaskFrameReadyEvent);
		MaskFrameReadyEvent = nullptr;
	}

	delete UpdateRegion;
	UpdateRegion = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::SetCameraProcessingEnabled(bool bEnable)
{
	bIsProcessingEnabled = bEnable;
	SetActive(bEnable);
	SetComponentTickEnabled(bEnable);
	if (CaptureComponent)
		CaptureComponent->bCaptureEveryFrame = bEnable;
}

void UUAVCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsProcessingEnabled) return;

	// Snapshot latest encoded results into tick-stable caches before processing.
	// Consumers calling GetRGBFrame() / GetMaskFrame() this tick see a consistent snapshot.
	{
		FScopeLock Lock(&LatestRGBMutex);
		if (bHasLatestRGBFrame)
		{
			TickRGBPayload   = LatestRGBPayload;
			TickRGBTimestamp = LatestRGBTimestamp;
			bHasTickRGBFrame = true;
		}
	}
	{
		FScopeLock Lock(&LatestMaskMutex);
		if (bHasLatestMaskFrame)
		{
			TickMaskPayload   = LatestMaskPayload;
			TickMaskTimestamp = LatestMaskTimestamp;
			bHasTickMaskFrame = true;
		}
	}

	ProcessFrame();
	CaptureMask();
}

// ─────────────────────────────────────────────────────────────────────────────
// RGB capture
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::ProcessFrame()
{
	if (!CaptureComponent || !RenderTarget) return;

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	TArray<FColor> ColorBuffer;
	RTResource->ReadPixels(ColorBuffer);
	if (ColorBuffer.Num() == 0) return;

	cv::Mat FrameBGRA(CVHeight, CVWidth, CV_8UC4, ColorBuffer.GetData());
	ProcessedFrameBuffer = FrameBGRA;

	if (RGBFrameReadyEvent)
	{
		const double Now = FPlatformTime::Seconds();
		if ((Now - LastRGBEncodeTime) >= MinEncodeInterval)
		{
			LastRGBEncodeTime = Now;
			{
				FScopeLock Lock(&RGBFrameMutex);
				FMemory::Memcpy(PendingRGBBGRA.GetData(), ProcessedFrameBuffer.data, CVWidth * CVHeight * 4);
				PendingRGBTimestamp = GetWorld()->GetTimeSeconds();
				bHasPendingRGBFrame = true;
			}
			RGBFrameReadyEvent->Trigger();
		}
	}

	UploadToTexture();
}

// ─────────────────────────────────────────────────────────────────────────────
// Mask capture — borrows CaptureComponent for one CaptureScene(), then restores.
// ReadPixels() flushes the render thread, so the CaptureScene() command is
// guaranteed to have executed before the pixel read — no cross-tick buffering
// needed. This keeps the mask frame aligned with the RGB frame from the same tick.
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::CaptureMask()
{
	if (!CaptureComponent || !MaskRenderTarget || !MaskPostProcessMaterial) return;
	if (!MaskFrameReadyEvent) return;

	const double Now = FPlatformTime::Seconds();
	if ((Now - LastMaskEncodeTime) < MinEncodeInterval) return;

	// Borrow CaptureComponent, point it at MaskRenderTarget, inject post-process material.
	UTextureRenderTarget2D*    OriginalRT         = CaptureComponent->TextureTarget;
	TArray<FWeightedBlendable> OriginalBlendables = CaptureComponent->PostProcessSettings.WeightedBlendables.Array;

	CaptureComponent->TextureTarget = MaskRenderTarget;
	CaptureComponent->PostProcessSettings.WeightedBlendables.Array.Add(
		FWeightedBlendable(1.0f, MaskPostProcessMaterial)
	);
	CaptureComponent->CaptureScene();

	// Restore immediately — the render command already holds its own reference.
	CaptureComponent->TextureTarget                                 = OriginalRT;
	CaptureComponent->PostProcessSettings.WeightedBlendables.Array = OriginalBlendables;

	// ReadPixels flushes all pending render commands, including the CaptureScene()
	// above, so MaskRenderTarget is fully populated before we read it.
	FTextureRenderTargetResource* RTResource = MaskRenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	TArray<FColor> ColorBuffer;
	RTResource->ReadPixels(ColorBuffer);
	if (ColorBuffer.Num() != CVWidth * CVHeight) return;

	LastMaskEncodeTime = Now;
	{
		FScopeLock Lock(&MaskFrameMutex);
		FMemory::Memcpy(PendingMaskBGRA.GetData(), ColorBuffer.GetData(), CVWidth * CVHeight * 4);
		PendingMaskTimestamp = GetWorld()->GetTimeSeconds();
		bHasPendingMaskFrame = true;
	}
	MaskFrameReadyEvent->Trigger();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public frame accessors — tick-stable, game thread only
// ─────────────────────────────────────────────────────────────────────────────

bool UUAVCameraComponent::GetRGBFrame(TArray<uint8>& OutPayload, double& OutTimestamp) const
{
	if (!bHasTickRGBFrame) return false;
	OutPayload   = TickRGBPayload;
	OutTimestamp = TickRGBTimestamp;
	return true;
}

bool UUAVCameraComponent::GetMaskFrame(TArray<uint8>& OutPayload, double& OutTimestamp) const
{
	if (!bHasTickMaskFrame) return false;
	OutPayload   = TickMaskPayload;
	OutTimestamp = TickMaskTimestamp;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RGB encoder thread
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::RGBEncoderLoop()
{
	TArray<uint8> LocalBGRA;
	double        LocalTimestamp = 0.0;

	IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(EImageFormat::JPEG);

	while (bRGBEncoderRunning)
	{
		RGBFrameReadyEvent->Wait(200);
		if (!bRGBEncoderRunning) break;

		{
			FScopeLock Lock(&RGBFrameMutex);
			if (!bHasPendingRGBFrame) continue;
			Swap(LocalBGRA, PendingRGBBGRA);
			PendingRGBBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);
			LocalTimestamp      = PendingRGBTimestamp;
			bHasPendingRGBFrame = false;
		}

		if (LocalBGRA.Num() != CVWidth * CVHeight * 4) continue;

		if (!Wrapper.IsValid() || !Wrapper->SetRaw(LocalBGRA.GetData(), LocalBGRA.Num(), CVWidth, CVHeight, ERGBFormat::BGRA, 8))
			continue;

		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(JpegQuality);

		{
			FScopeLock Lock(&LatestRGBMutex);
			LatestRGBPayload.Reset();
			LatestRGBPayload.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
			LatestRGBTimestamp = LocalTimestamp;
			bHasLatestRGBFrame = true;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Mask encoder thread
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::MaskEncoderLoop()
{
	TArray<uint8> LocalBGRA;
	double        LocalTimestamp = 0.0;

	IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(EImageFormat::JPEG);

	while (bMaskEncoderRunning)
	{
		MaskFrameReadyEvent->Wait(200);
		if (!bMaskEncoderRunning) break;

		{
			FScopeLock Lock(&MaskFrameMutex);
			if (!bHasPendingMaskFrame) continue;
			Swap(LocalBGRA, PendingMaskBGRA);
			PendingMaskBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);
			LocalTimestamp       = PendingMaskTimestamp;
			bHasPendingMaskFrame = false;
		}

		if (LocalBGRA.Num() != CVWidth * CVHeight * 4) continue;

		if (!Wrapper.IsValid() || !Wrapper->SetRaw(LocalBGRA.GetData(), LocalBGRA.Num(), CVWidth, CVHeight, ERGBFormat::BGRA, 8))
			continue;

		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(JpegQuality);

		{
			FScopeLock Lock(&LatestMaskMutex);
			LatestMaskPayload.Reset();
			LatestMaskPayload.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
			LatestMaskTimestamp = LocalTimestamp;
			bHasLatestMaskFrame = true;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::ComputeFOV(float HFovDeg)
{
	HorizontalFOVDeg = HFovDeg;

	const float AspectRatio = static_cast<float>(CVWidth) / static_cast<float>(CVHeight);
	const float HFovRad     = FMath::DegreesToRadians(HFovDeg);
	VerticalFOVDeg = FMath::RadiansToDegrees(
		2.0f * FMath::Atan(FMath::Tan(HFovRad * 0.5f) / AspectRatio)
	);
}

void UUAVCameraComponent::UploadToTexture()
{
	if (!OutputTexture || ProcessedFrameBuffer.empty()) return;

	FTexture2DMipMap& Mip = OutputTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, ProcessedFrameBuffer.data, ProcessedFrameBuffer.total() * ProcessedFrameBuffer.elemSize());
	Mip.BulkData.Unlock();
	OutputTexture->UpdateResource();
}
