// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VideoStreamingComponent.generated.h"

// Forward-declare the ZMQ state struct to keep zmq.hpp (and windows.h) out of this header.
struct FZmqSocketState;

UCLASS(ClassGroup=(UAV), meta=(BlueprintSpawnableComponent))
class UAVSIMULATOR_API UVideoStreamingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVideoStreamingComponent();

	/** ZMQ PUB endpoint. Python connects with zmq.SUB. Example: "tcp://*:5555" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	FString Endpoint = TEXT("tcp://*:5555");

	/** JPEG quality (1–100). Lower value = smaller packets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 100))
	int32 JpegQuality = 80;

	/** Maximum frames per second forwarded over ZMQ. Excess frames are silently dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = 1, ClampMax = 120))
	int32 MaxStreamFPS = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 Width = 640;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	int32 Height = 480;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Called on the game thread by UAVCameraComponent's OnFrameReady delegate. */
	void OnFrameReady(TArrayView<const uint8> BGRAData);

	/** Entry point for the dedicated sender thread. */
	void SenderLoop();

	// ── ZMQ ─────────────────────────────────────────────────────────────────
	FZmqSocketState* ZmqState = nullptr;

	// ── Frame buffer ────────────────────────────────────────────────────────
	// Game thread Memcpy's into PendingBGRA under FrameMutex.
	// Sender thread swaps PendingBGRA into its own LocalBGRA (O(1)) under the
	// same lock, then encodes entirely outside the lock — no shared data access.
	TArray<uint8>  PendingBGRA;               // guarded by FrameMutex
	bool           bHasPendingFrame = false;  // guarded by FrameMutex
	FCriticalSection FrameMutex;

	// ── Sender thread ────────────────────────────────────────────────────────
	FEvent*          FrameReadyEvent = nullptr;
	FThreadSafeBool  bSenderRunning;
	FRunnable*       SenderRunnable  = nullptr;
	FRunnableThread* SenderThread    = nullptr;

	// ── Frame-rate limiter (game thread side) ────────────────────────────────
	double MinSendInterval   = 1.0 / 30.0;
	double LastFrameSentTime = 0.0;
};
