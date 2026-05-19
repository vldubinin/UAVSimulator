#include "SegmentationMaskCameraComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/RunnableThread.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Minimal FRunnable wrapper (local to this TU)
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

USegmentationMaskCameraComponent::USegmentationMaskCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USegmentationMaskCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	CaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	if (!CaptureComponent)
	{
		UE_LOG(LogUAV, Error, TEXT("SegmentationMaskCameraComponent: USceneCaptureComponent2D not found on %s."), *Owner->GetName());
		SetComponentTickEnabled(false);
		return;
	}

	// Private render target — CaptureComponent is temporarily pointed here during each mask capture.
	MaskRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	MaskRenderTarget->InitCustomFormat(MaskWidth, MaskHeight, PF_B8G8R8A8, false);
	MaskRenderTarget->UpdateResourceImmediate(false);

	// Must tick after UAVCameraComponent so ProcessFrame() reads its RT before we borrow the capture component.
	if (UUAVCameraComponent* CamComp = Owner->FindComponentByClass<UUAVCameraComponent>())
		AddTickPrerequisiteComponent(CamComp);

	MinEncodeInterval = 1.0 / FMath::Max(MaxEncodeFPS, 1);
	LastEncodeTime    = -MinEncodeInterval;
	PendingBGRA.SetNumUninitialized(MaskWidth * MaskHeight * 4);

	FrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bEncoderRunning = true;
	EncoderRunnable = new FLambdaRunnable([this]() { EncoderLoop(); });
	EncoderThread   = FRunnableThread::Create(EncoderRunnable, TEXT("UAV_SegMaskEncoder"), 0, TPri_BelowNormal);
}

void USegmentationMaskCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EncoderThread)
	{
		bEncoderRunning = false;
		FrameReadyEvent->Trigger();
		EncoderThread->WaitForCompletion();
		delete EncoderThread;
		EncoderThread = nullptr;
	}

	delete EncoderRunnable;
	EncoderRunnable = nullptr;

	if (FrameReadyEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(FrameReadyEvent);
		FrameReadyEvent = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick — read previous frame's mask, then schedule this frame's mask capture
// ─────────────────────────────────────────────────────────────────────────────

void USegmentationMaskCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CaptureComponent || !MaskRenderTarget || !MaskPostProcessMaterial) return;

	// ── 1. Read the mask captured in the previous frame ────────────────────────
	// bHasPendingCapture guards the first tick where MaskRenderTarget is empty.
	if (bHasPendingCapture && FrameReadyEvent)
	{
		const double Now = FPlatformTime::Seconds();
		if ((Now - LastEncodeTime) >= MinEncodeInterval)
		{
			FTextureRenderTargetResource* RTResource = MaskRenderTarget->GameThread_GetRenderTargetResource();
			if (RTResource)
			{
				TArray<FColor> ColorBuffer;
				RTResource->ReadPixels(ColorBuffer);
				if (ColorBuffer.Num() == MaskWidth * MaskHeight)
				{
					LastEncodeTime = Now;
					{
						FScopeLock Lock(&FrameMutex);
						FMemory::Memcpy(PendingBGRA.GetData(), ColorBuffer.GetData(), MaskWidth * MaskHeight * 4);
						PendingTimestamp = GetWorld()->GetTimeSeconds();
						bHasPendingFrame = true;
					}
					FrameReadyEvent->Trigger();
				}
			}
		}
	}

	// ── 2. Schedule mask capture for this frame ─────────────────────────────────
	// Save only what we modify so the restore is surgical.
	UTextureRenderTarget2D*    OriginalRT         = CaptureComponent->TextureTarget;
	TArray<FWeightedBlendable> OriginalBlendables = CaptureComponent->PostProcessSettings.WeightedBlendables.Array;

	// Point the capture component at our mask render target and inject the mask material.
	CaptureComponent->TextureTarget = MaskRenderTarget;
	CaptureComponent->PostProcessSettings.WeightedBlendables.Array.Add(
		FWeightedBlendable(1.0f, MaskPostProcessMaterial)
	);

	// CaptureScene() enqueues a render command that captures the current component
	// state (TextureTarget + PostProcessSettings) into MaskRenderTarget.
	CaptureComponent->CaptureScene();

	// Restore immediately — the render command already holds a reference to MaskRenderTarget,
	// so UAVCameraComponent's bCaptureEveryFrame will render to the original RT unaffected.
	CaptureComponent->TextureTarget                                 = OriginalRT;
	CaptureComponent->PostProcessSettings.WeightedBlendables.Array = OriginalBlendables;

	bHasPendingCapture = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — called on the game thread by SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool USegmentationMaskCameraComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	FScopeLock Lock(&LatestFrameMutex);
	if (!bHasLatestFrame) return false;

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestEncodedTimestamp;
	OutFrame.Payload   = LatestEncodedPayload;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Encoder thread — BGRA → JPEG
// ─────────────────────────────────────────────────────────────────────────────

void USegmentationMaskCameraComponent::EncoderLoop()
{
	TArray<uint8> LocalBGRA;
	double        LocalTimestamp = 0.0;

	IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(EImageFormat::JPEG);

	while (bEncoderRunning)
	{
		FrameReadyEvent->Wait(200);
		if (!bEncoderRunning) break;

		{
			FScopeLock Lock(&FrameMutex);
			if (!bHasPendingFrame) continue;
			Swap(LocalBGRA, PendingBGRA);
			PendingBGRA.SetNumUninitialized(MaskWidth * MaskHeight * 4);
			LocalTimestamp   = PendingTimestamp;
			bHasPendingFrame = false;
		}

		if (LocalBGRA.Num() != MaskWidth * MaskHeight * 4) continue;

		if (!Wrapper.IsValid() || !Wrapper->SetRaw(LocalBGRA.GetData(), LocalBGRA.Num(), MaskWidth, MaskHeight, ERGBFormat::BGRA, 8))
			continue;

		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(JpegQuality);

		{
			FScopeLock Lock(&LatestFrameMutex);
			LatestEncodedPayload.Reset();
			LatestEncodedPayload.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
			LatestEncodedTimestamp = LocalTimestamp;
			bHasLatestFrame = true;
		}
	}
}
