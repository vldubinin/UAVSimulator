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
	OutputTexture    = nullptr;
}

void UUAVCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	CaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	if (!CaptureComponent)
	{
		UE_LOG(LogUAV, Error, TEXT("UAVCameraComponent: USceneCaptureComponent2D not found on %s. Add one in the Blueprint."), *Owner->GetName());
		return;
	}

	UE_LOG(LogUAV, Log, TEXT("UAVCameraComponent: Camera found on %s"), *Owner->GetName());

	RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitCustomFormat(CVWidth, CVHeight, PF_B8G8R8A8, false);
	RenderTarget->UpdateResourceImmediate(false);
	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

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

	MinEncodeInterval = 1.0 / FMath::Max(MaxEncodeFPS, 1);
	LastEncodeTime    = -MinEncodeInterval;
	PendingBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);

	FrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bEncoderRunning = true;
	EncoderRunnable = new FLambdaRunnable([this]() { EncoderLoop(); });
	EncoderThread   = FRunnableThread::Create(EncoderRunnable, TEXT("UAV_CameraEncoder"), 0, TPri_BelowNormal);
}

void UUAVCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

	delete UpdateRegion;
	UpdateRegion = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick / capture
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
	ProcessFrame();
}

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

	// Post to encoding thread at the configured FPS cap
	if (FrameReadyEvent)
	{
		const double Now = FPlatformTime::Seconds();
		if ((Now - LastEncodeTime) >= MinEncodeInterval)
		{
			LastEncodeTime = Now;
			{
				FScopeLock Lock(&FrameMutex);
				FMemory::Memcpy(PendingBGRA.GetData(), ProcessedFrameBuffer.data, CVWidth * CVHeight * 4);
				PendingTimestamp = GetWorld()->GetTimeSeconds();
				bHasPendingFrame = true;
			}
			FrameReadyEvent->Trigger();
		}
	}

	UploadToTexture();
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — called on game thread by SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool UUAVCameraComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	FScopeLock Lock(&LatestFrameMutex);
	if (!bHasLatestFrame) return false;

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestEncodedTimestamp;
	OutFrame.Payload   = LatestEncodedPayload;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Encoder thread — compresses BGRA to JPEG, stores result in LatestEncodedPayload
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::EncoderLoop()
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
			PendingBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);
			LocalTimestamp   = PendingTimestamp;
			bHasPendingFrame = false;
		}

		if (LocalBGRA.Num() != CVWidth * CVHeight * 4) continue;

		if (!Wrapper.IsValid() || !Wrapper->SetRaw(LocalBGRA.GetData(), LocalBGRA.Num(), CVWidth, CVHeight, ERGBFormat::BGRA, 8))
			continue;

		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(JpegQuality);

		{
			FScopeLock Lock(&LatestFrameMutex);
			LatestEncodedPayload.Reset();
			LatestEncodedPayload.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
			LatestEncodedTimestamp = LocalTimestamp;
			bHasLatestFrame        = true;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU upload
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::UploadToTexture()
{
	if (!OutputTexture || ProcessedFrameBuffer.empty()) return;

	FTexture2DMipMap& Mip = OutputTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, ProcessedFrameBuffer.data, ProcessedFrameBuffer.total() * ProcessedFrameBuffer.elemSize());
	Mip.BulkData.Unlock();
	OutputTexture->UpdateResource();
}
