#include "UAVCameraComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"

UUAVCameraComponent::UUAVCameraComponent()
{
	// Компонент обробляє кадри лише за явним викликом ProcessFrame — власний тік не потрібен
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

	// Шукаємо наявний USceneCaptureComponent2D на акторі (додається в Blueprint)
	CaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	if (!CaptureComponent)
	{
		UE_LOG(LogUAV, Error, TEXT("UAVCameraComponent: USceneCaptureComponent2D not found on %s. Add one in the Blueprint."), *Owner->GetName());
		return;
	}

	UE_LOG(LogUAV, Log, TEXT("UAVCameraComponent: Camera found on %s"), *Owner->GetName());

	// Створюємо рендер-таргет як ціль для захоплення сцени
	RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitCustomFormat(CVWidth, CVHeight, PF_B8G8R8A8, false);
	RenderTarget->UpdateResourceImmediate(false);
	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// Вихідна текстура: у неї будуть завантажуватись оброблені OpenCV кадри
	OutputTexture = UTexture2D::CreateTransient(CVWidth, CVHeight, PF_B8G8R8A8);
	OutputTexture->UpdateResource();

	UpdateRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, CVWidth, CVHeight);
}

void UUAVCameraComponent::ProcessFrame()
{
	if (!CaptureComponent || !RenderTarget) return;

	// Отримуємо ресурс рендер-таргету на ігровому потоці
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	// Зчитуємо пікселі рендер-таргету в буфер CPU
	TArray<FColor> ColorBuffer;
	RTResource->ReadPixels(ColorBuffer);
	if (ColorBuffer.Num() == 0) return;

	// Обгортаємо буфер у cv::Mat без копіювання (BGRA, 4 канали)
	cv::Mat FrameBGRA(CVHeight, CVWidth, CV_8UC4, ColorBuffer.GetData());
	cv::Mat CameraFrame;
	// Горизонтальне дзеркалювання (параметр 1 = по осі Y)
	cv::flip(FrameBGRA, CameraFrame, 1);

	// Накладаємо текстову мітку по центру кадру
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

	// Блокуємо mip-рівень 0 для запису та копіюємо дані OpenCV-буфера
	FTexture2DMipMap& Mip = OutputTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, ProcessedFrameBuffer.data, ProcessedFrameBuffer.total() * ProcessedFrameBuffer.elemSize());
	Mip.BulkData.Unlock();
	// Надсилаємо оновлені дані на GPU
	OutputTexture->UpdateResource();
}
