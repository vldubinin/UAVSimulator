#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include "PostOpenCVHeaders.h"

#include "UAVCameraComponent.generated.h"

/**
 * Manages the onboard camera: render target, OpenCV image processing, and output texture.
 * Add this component to the aircraft pawn. It will find the first USceneCaptureComponent2D
 * on the owner at BeginPlay.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UUAVCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUAVCameraComponent();

	/** Capture the current frame, run OpenCV processing, and update OutputTexture. */
	void ProcessFrame();

protected:
	virtual void BeginPlay() override;

public:
	/** The processed output texture — bind this in a widget or material. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	UTexture2D* OutputTexture;

private:
	void UploadToTexture();

	UPROPERTY()
	USceneCaptureComponent2D* CaptureComponent;

	UPROPERTY()
	UTextureRenderTarget2D* RenderTarget;

	FUpdateTextureRegion2D* UpdateRegion;

	cv::Mat ProcessedFrameBuffer;

	static constexpr int32 CVWidth  = 640;
	static constexpr int32 CVHeight = 480;
};
