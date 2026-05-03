// Fill out your copyright notice in the Description page of Project Settings.

#include "UAVSimulator/Components/VideoStreamingComponent.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/RunnableThread.h"

// Нативні модулі UE для роботи із зображеннями (замість OpenCV)
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include <zmq.hpp>
THIRD_PARTY_INCLUDES_END

// ─────────────────────────────────────────────────────────────────────────────
// ZMQ state — defined here so zmq.hpp never leaks into the component header.
// ─────────────────────────────────────────────────────────────────────────────

struct FZmqSocketState
{
	zmq::context_t Context{ 1 };
	zmq::socket_t  Socket;

	explicit FZmqSocketState(const FString& Endpoint)
		: Socket(Context, ZMQ_PUB)
	{
		int Hwm = 2;
		Socket.setsockopt(ZMQ_SNDHWM, &Hwm, sizeof(Hwm));
		Socket.bind(TCHAR_TO_UTF8(*Endpoint));
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Minimal FRunnable that delegates to a TUniqueFunction.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	class FLambdaRunnable final : public FRunnable
	{
	public:
		explicit FLambdaRunnable(TUniqueFunction<void()> InBody)
			: Body(MoveTemp(InBody)) {
		}

		virtual uint32 Run() override { Body(); return 0; }

	private:
		TUniqueFunction<void()> Body;
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// Component
// ─────────────────────────────────────────────────────────────────────────────

UVideoStreamingComponent::UVideoStreamingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVideoStreamingComponent::BeginPlay()
{
	Super::BeginPlay();

	MinSendInterval = 1.0 / FMath::Max(MaxStreamFPS, 1);
	LastFrameSentTime = -MinSendInterval;

	PendingBGRA.SetNumUninitialized(Width * Height * 4);

	try
	{
		ZmqState = new FZmqSocketState(Endpoint);
		UE_LOG(LogTemp, Log, TEXT("VideoStreamingComponent: ZMQ PUB bound to %s"), *Endpoint);
	}
	catch (const zmq::error_t& E)
	{
		UE_LOG(LogTemp, Error, TEXT("VideoStreamingComponent: ZMQ bind failed — %hs"), E.what());
		return;
	}

	FrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bSenderRunning = true;
	SenderRunnable = new FLambdaRunnable([this]() { SenderLoop(); });
	SenderThread = FRunnableThread::Create(SenderRunnable, TEXT("UAV_VideoStreamSender"),
		0, TPri_BelowNormal);

	if (UUAVCameraComponent* Cam = GetOwner()->FindComponentByClass<UUAVCameraComponent>())
	{
		Cam->OnFrameReady.AddUObject(this, &UVideoStreamingComponent::OnFrameReady);
		UE_LOG(LogTemp, Log, TEXT("VideoStreamingComponent: subscribed to camera on %s"),
			*GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VideoStreamingComponent: no UUAVCameraComponent on %s"),
			*GetOwner()->GetName());
	}
}

void UVideoStreamingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Owner = GetOwner())
	{
		if (UUAVCameraComponent* Cam = Owner->FindComponentByClass<UUAVCameraComponent>())
		{
			Cam->OnFrameReady.RemoveAll(this);
		}
	}

	if (SenderThread)
	{
		bSenderRunning = false;
		FrameReadyEvent->Trigger();
		SenderThread->WaitForCompletion();
		delete SenderThread;
		SenderThread = nullptr;
	}

	delete SenderRunnable;
	SenderRunnable = nullptr;

	if (FrameReadyEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(FrameReadyEvent);
		FrameReadyEvent = nullptr;
	}

	delete ZmqState;
	ZmqState = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Game-thread callback
// ─────────────────────────────────────────────────────────────────────────────

void UVideoStreamingComponent::OnFrameReady(TArrayView<const uint8> BGRAData)
{
	if (!ZmqState || !bSenderRunning) return;

	const double Now = FPlatformTime::Seconds();
	if ((Now - LastFrameSentTime) < MinSendInterval) return;
	LastFrameSentTime = Now;

	const int32 CopyBytes = FMath::Min(BGRAData.Num(), PendingBGRA.Num());
	{
		FScopeLock Lock(&FrameMutex);
		FMemory::Memcpy(PendingBGRA.GetData(), BGRAData.GetData(), CopyBytes);
		bHasPendingFrame = true;
	}

	FrameReadyEvent->Trigger();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sender thread — encodes JPEG and pushes over ZMQ.
// ─────────────────────────────────────────────────────────────────────────────

void UVideoStreamingComponent::SenderLoop()
{
	TArray<uint8> LocalBGRA;

	// Ініціалізуємо модуль ImageWrapper один раз для потоку
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

	while (bSenderRunning)
	{
		FrameReadyEvent->Wait(200);

		if (!bSenderRunning) break;

		{
			FScopeLock Lock(&FrameMutex);
			if (!bHasPendingFrame) continue;
			Swap(LocalBGRA, PendingBGRA);

			// Відновлюємо розмір PendingBGRA, оскільки Swap забрав його пам'ять
			PendingBGRA.SetNumUninitialized(Width * Height * 4);
			bHasPendingFrame = false;
		}

		if (LocalBGRA.Num() != Width * Height * 4) continue;

		// Стискаємо сирі BGRA байти у JPEG
		if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(LocalBGRA.GetData(), LocalBGRA.Num(), Width, Height, ERGBFormat::BGRA, 8))
		{
			// Отримуємо масив байтів стисненого JPEG (якість 80)
			const TArray64<uint8>& JPEGData = ImageWrapper->GetCompressed(80);

			try
			{
				zmq::message_t Msg(JPEGData.GetData(), static_cast<size_t>(JPEGData.Num()));
				ZmqState->Socket.send(Msg, ZMQ_DONTWAIT);
			}
			catch (const zmq::error_t&)
			{
				// Ігноруємо EAGAIN
			}
		}
	}
}