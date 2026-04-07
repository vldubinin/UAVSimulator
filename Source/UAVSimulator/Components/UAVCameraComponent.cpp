#include "UAVCameraComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"

UUAVCameraComponent::UUAVCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CaptureComponent  = nullptr;
	RenderTarget      = nullptr;
	UpdateRegion      = nullptr;
	OutputTexture     = nullptr;
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

	UpdateRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, CVWidth, CVHeight);
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
	cv::Mat CameraFrame;
	cv::flip(FrameBGRA, CameraFrame, 1);

	// Overlay label
	const std::string LabelText = "VIRTUAL CAM";
	const int FontFace = cv::FONT_HERSHEY_SIMPLEX;
	const double FontScale = 1.0;
	const int Thickness = 2;
	int Baseline = 0;
	cv::Size TextSize = cv::getTextSize(LabelText, FontFace, FontScale, Thickness, &Baseline);
	cv::Point TextOrg((CameraFrame.cols - TextSize.width) / 2, (CameraFrame.rows + TextSize.height) / 2);
	cv::putText(CameraFrame, LabelText, TextOrg, FontFace, FontScale, cv::Scalar(251, 205, 210), Thickness, cv::LINE_AA);

	ProcessedFrameBuffer = CameraFrame;
	UploadToTexture();
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
