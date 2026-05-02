#include "UAVCameraComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"

UUAVCameraComponent::UUAVCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
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

	if (FTexture2DMipMap* Mip = &OutputTexture->GetPlatformData()->Mips[0])
	{
		void* Data = Mip->BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memset(Data, 0, CVWidth * CVHeight * 4);
		Mip->BulkData.Unlock();
	}
	OutputTexture->UpdateResource();

	UpdateRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, CVWidth, CVHeight);
}

void UUAVCameraComponent::SetCameraProcessingEnabled(bool bEnable)
{
	bIsProcessingEnabled = bEnable;
	SetActive(bEnable);
	SetComponentTickEnabled(bEnable);
	if (CaptureComponent)
	{
		CaptureComponent->bCaptureEveryFrame = bEnable;
	}
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

	// Отримуємо ресурс рендер-таргету на ігровому потоці
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	// Зчитуємо пікселі рендер-таргету в буфер CPU
	TArray<FColor> ColorBuffer;
	RTResource->ReadPixels(ColorBuffer);
	if (ColorBuffer.Num() == 0) return;

	// Обгортаємо буфер у cv::Mat без копіювання (BGRA, 4 канали)
	cv::Mat FrameBGRA(CVHeight, CVWidth, CV_8UC4, ColorBuffer.GetData());

	ProcessedFrameBuffer = FrameBGRA;

	if (OnFrameReady.IsBound())
	{
		OnFrameReady.Broadcast(TArrayView<const uint8>(
			ProcessedFrameBuffer.data,
			static_cast<int32>(ProcessedFrameBuffer.total() * ProcessedFrameBuffer.elemSize())
		));
	}

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
