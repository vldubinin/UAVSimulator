// Fill out your copyright notice in the Description page of Project Settings.

#include "UAVSimulator/Components/VideoStreamingComponent.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/RunnableThread.h"

#include "PreOpenCVHeaders.h"
#include <opencv2/imgproc.hpp>
#include "PostOpenCVHeaders.h"

THIRD_PARTY_INCLUDES_START
#include <zmq.hpp>
THIRD_PARTY_INCLUDES_END

// ─────────────────────────────────────────────────────────────────────────────
// ZMQ state — defined here so zmq.hpp never leaks into the component header.
// ─────────────────────────────────────────────────────────────────────────────

struct FZmqSocketState
{
	zmq::context_t Context{ 1 }; // 1 I/O thread is enough for a single socket
	zmq::socket_t  Socket;

	explicit FZmqSocketState(const FString& Endpoint)
		: Socket(Context, ZMQ_PUB)
	{
		// Keep at most 2 unsent messages in the ZMQ send queue.
		// When the subscriber is too slow, older frames are dropped automatically.
		int Hwm = 2;
		Socket.setsockopt(ZMQ_SNDHWM, &Hwm, sizeof(Hwm));
		Socket.bind(TCHAR_TO_UTF8(*Endpoint));
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Minimal FRunnable that delegates to a TUniqueFunction.
// Avoids the need for a friend declaration or making SenderLoop() public.
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
// Component
// ─────────────────────────────────────────────────────────────────────────────

UVideoStreamingComponent::UVideoStreamingComponent()
{
	// All work is done on the dedicated sender thread; no per-frame game-thread tick needed.
	PrimaryComponentTick.bCanEverTick = false;
}

void UVideoStreamingComponent::BeginPlay()
{
	Super::BeginPlay();

	MinSendInterval   = 1.0 / FMath::Max(MaxStreamFPS, 1);
	LastFrameSentTime = -MinSendInterval; // allow sending on the very first frame

	// Pre-allocate the shared frame buffer. No allocation on the hot path after this.
	PendingBGRA.SetNumUninitialized(Width * Height * 4);

	// Bind the ZMQ publisher socket.
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

	// Start the dedicated sender thread.
	FrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bSenderRunning  = true;
	SenderRunnable  = new FLambdaRunnable([this]() { SenderLoop(); });
	SenderThread    = FRunnableThread::Create(SenderRunnable, TEXT("UAV_VideoStreamSender"),
	                                           0, TPri_BelowNormal);

	// Subscribe to the first UAVCameraComponent found on the same actor.
	// UAVCameraComponent has no knowledge of this component — coupling is one-way.
	if (UUAVCameraComponent* Cam = GetOwner()->FindComponentByClass<UUAVCameraComponent>())
	{
		Cam->OnFrameReady.AddUObject(this, &UVideoStreamingComponent::OnFrameReady);
		UE_LOG(LogTemp, Log, TEXT("VideoStreamingComponent: subscribed to camera on %s"),
		       *GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VideoStreamingComponent: no UUAVCameraComponent on %s — "
		       "add the component or call AddUObject(this, &UVideoStreamingComponent::OnFrameReady) manually."),
		       *GetOwner()->GetName());
	}
}

void UVideoStreamingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unsubscribe first so no new frames arrive while we're tearing down.
	if (AActor* Owner = GetOwner())
	{
		if (UUAVCameraComponent* Cam = Owner->FindComponentByClass<UUAVCameraComponent>())
		{
			Cam->OnFrameReady.RemoveAll(this);
		}
	}

	// Signal the sender thread to exit and wait for it.
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
// Game-thread callback — called by UAVCameraComponent after each processed frame.
// ─────────────────────────────────────────────────────────────────────────────

void UVideoStreamingComponent::OnFrameReady(TArrayView<const uint8> BGRAData)
{
	if (!ZmqState || !bSenderRunning) return;

	// Rate limiter: drop frames that arrive faster than MaxStreamFPS.
	const double Now = FPlatformTime::Seconds();
	if ((Now - LastFrameSentTime) < MinSendInterval) return;
	LastFrameSentTime = Now;

	// Write the new frame into PendingBGRA under the lock.
	// The sender thread never touches PendingBGRA outside the lock — it swaps
	// the whole TArray out in O(1), so this critical section is always brief.
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
	// LocalBGRA is exclusively owned by this thread — no lock needed when converting.
	TArray<uint8> LocalBGRA;
	cv::Mat       MatBGR(Height, Width, CV_8UC3);

	while (bSenderRunning)
	{
		// Wait up to 200 ms so graceful shutdown is never stuck longer than that.
		FrameReadyEvent->Wait(200);

		if (!bSenderRunning) break;

		// Swap PendingBGRA into LocalBGRA — O(1), no pixel copy in the lock.
		{
			FScopeLock Lock(&FrameMutex);
			if (!bHasPendingFrame) continue;
			Swap(LocalBGRA, PendingBGRA);
			bHasPendingFrame = false;
		}

		if (LocalBGRA.Num() != Width * Height * 4) continue;

		// BGRA → BGR, then send raw BGR bytes.
		// UE's bundled OpenCV is built without libjpeg/libpng so imencode is unavailable.
		// Python receiver: np.frombuffer(msg, dtype=np.uint8).reshape(Height, Width, 3)
		cv::Mat MatBGRA(Height, Width, CV_8UC4, LocalBGRA.GetData());
		cv::cvtColor(MatBGRA, MatBGR, cv::COLOR_BGRA2BGR);

		try
		{
			zmq::message_t Msg(MatBGR.data, static_cast<size_t>(Width * Height * 3));
			ZmqState->Socket.send(Msg, ZMQ_DONTWAIT);
		}
		catch (const zmq::error_t&)
		{
			// EAGAIN is expected when no subscriber is connected yet; ignore.
		}
	}
}
