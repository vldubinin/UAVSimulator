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
	/** Ініціалізує внутрішні вказівники нулями; фактична ініціалізація відбувається у BeginPlay. */
	UUAVCameraComponent();

	/**
	 * Захоплює поточний кадр з рендер-таргету, виконує обробку OpenCV
	 * (дзеркальне відображення, накладення тексту "VIRTUAL CAM") та оновлює OutputTexture.
	 * Безпечно викликати щотіку; повертається одразу якщо камера не ініціалізована.
	 */
	void ProcessFrame();

protected:
	/**
	 * Знаходить USceneCaptureComponent2D на власнику, створює UTextureRenderTarget2D
	 * (640×480, PF_B8G8R8A8) та вихідну UTexture2D для прив'язки в Blueprint.
	 */
	virtual void BeginPlay() override;

public:
	/** Оброблена вихідна текстура — прив'яжіть у віджеті або матеріалі. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	UTexture2D* OutputTexture;

private:
	/**
	 * Копіює дані з ProcessedFrameBuffer у mip-рівень OutputTexture та викликає UpdateResource.
	 * Викликається лише з ProcessFrame після успішної обробки кадру.
	 */
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
