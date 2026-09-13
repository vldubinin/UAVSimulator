#include "UAVCameraComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"
#include "HAL/RunnableThread.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/World.h"
#include "Components/LineBatchComponent.h"
#include "CesiumCameraManager.h"
#include "CesiumCamera.h"

// ─────────────────────────────────────────────────────────────────────────────
// Мінімальна обгортка FRunnable
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
// Життєвий цикл
// ─────────────────────────────────────────────────────────────────────────────

UUAVCameraComponent::UUAVCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	CaptureComponent = nullptr;
	RenderTarget     = nullptr;
	MaskRenderTarget = nullptr;
	OutputTexture    = nullptr;
	ComputeFOV(90.0f);
}

void UUAVCameraComponent::OnRegister()
{
	Super::OnRegister();

	AActor* Owner = GetOwner();
	if (Owner)
	{
		if (USceneCaptureComponent2D* Comp = Owner->FindComponentByClass<USceneCaptureComponent2D>())
			ComputeFOV(Comp->FOVAngle);
	}
}

void UUAVCameraComponent::OnUnregister()
{
	UnregisterCesiumSceneCaptureCamera();
	Super::OnUnregister();
}

#if WITH_EDITOR
void UUAVCameraComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	AActor* Owner = GetOwner();
	if (Owner)
	{
		if (USceneCaptureComponent2D* Comp = Owner->FindComponentByClass<USceneCaptureComponent2D>())
			ComputeFOV(Comp->FOVAngle);
	}
}
#endif

void UUAVCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	CaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	if (!CaptureComponent)
	{
		return;
	}

	ComputeFOV(CaptureComponent->FOVAngle);
	LogCameraIntrinsics();

	// Відладочні фігури (DrawDebugLine/Box/Sphere/...) рендеряться в компоненти line-batch
	// світу, які є звичайними UPrimitiveComponent без актора-власника — захоплення сцени не
	// має автоматичного способу їх виключити, тож будь-яка відладочна візуалізація, намальована
	// деінде (наприклад, промені UCesiumSurroundingsScannerComponent), інакше з'явилася б прямо
	// в потоці цієї камери. Явно ховаємо всі три типи line-batcher.
	if (UWorld* World = GetWorld())
	{
		if (ULineBatchComponent* LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::World))
			CaptureComponent->HideComponent(LineBatcher);
		if (ULineBatchComponent* PersistentLineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
			CaptureComponent->HideComponent(PersistentLineBatcher);
		if (ULineBatchComponent* ForegroundLineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::Foreground))
			CaptureComponent->HideComponent(ForegroundLineBatcher);
	}

	// RGB рендер-таргет
	RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitCustomFormat(CVWidth, CVHeight, PF_B8G8R8A8, false);
	RenderTarget->UpdateResourceImmediate(false);
	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// Вихідна текстура
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

	// Рендер-таргет маски — завжди виділяється; захоплення обумовлене наявністю MaskPostProcessMaterial
	MaskRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	MaskRenderTarget->InitCustomFormat(CVWidth, CVHeight, PF_B8G8R8A8, false);
	MaskRenderTarget->UpdateResourceImmediate(false);

	MinEncodeInterval  = 1.0 / FMath::Max(MaxEncodeFPS, 1);
	LastRGBEncodeTime  = -MinEncodeInterval;
	LastMaskEncodeTime = -MinEncodeInterval;
	PendingRGBBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);
	PendingMaskBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);

	// Потік кодувальника RGB
	RGBFrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bRGBEncoderRunning = true;
	RGBEncoderRunnable = new FLambdaRunnable([this]() { RGBEncoderLoop(); });
	RGBEncoderThread   = FRunnableThread::Create(RGBEncoderRunnable, TEXT("UAV_RGBEncoder"), 0, TPri_BelowNormal);

	// Потік кодувальника маски
	MaskFrameReadyEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
	bMaskEncoderRunning = true;
	MaskEncoderRunnable = new FLambdaRunnable([this]() { MaskEncoderLoop(); });
	MaskEncoderThread   = FRunnableThread::Create(MaskEncoderRunnable, TEXT("UAV_MaskEncoder"), 0, TPri_BelowNormal);

	// Реєструємо це захоплення сцени як камеру Cesium, щоб тайли в її полі зору уточнювалися
	// незалежно від фрустуму основної камери гравця (SyncCesiumSceneCaptureCamera повторно
	// розв'язує менеджер, якщо це зараз не вдасться).
	ResolveCesiumCameraManager();
}

void UUAVCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterCesiumSceneCaptureCamera();

	if (RGBEncoderThread)
	{
		bRGBEncoderRunning = false;
		if (RGBFrameReadyEvent) RGBFrameReadyEvent->Trigger();
		RGBEncoderThread->WaitForCompletion();
		delete RGBEncoderThread;
		RGBEncoderThread = nullptr;
	}
	delete RGBEncoderRunnable;
	RGBEncoderRunnable = nullptr;

	if (MaskEncoderThread)
	{
		bMaskEncoderRunning = false;
		if (MaskFrameReadyEvent) MaskFrameReadyEvent->Trigger();
		MaskEncoderThread->WaitForCompletion();
		delete MaskEncoderThread;
		MaskEncoderThread = nullptr;
	}
	delete MaskEncoderRunnable;
	MaskEncoderRunnable = nullptr;

	if (RGBFrameReadyEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(RGBFrameReadyEvent);
		RGBFrameReadyEvent = nullptr;
	}
	if (MaskFrameReadyEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(MaskFrameReadyEvent);
		MaskFrameReadyEvent = nullptr;
	}

	delete UpdateRegion;
	UpdateRegion = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Реєстрація камери захоплення сцени Cesium
//
// ACesium3DTileset у Cesium подає вибір тайлів / LOD / відсіювання лише з камер гравця,
// вікон перегляду редактора та окремих *акторів* ASceneCapture2D. Наше захоплення —
// це USceneCaptureComponent2D, що живе всередині Blueprint пешки, тож Cesium його ніколи не
// бачить, і тайли в напрямку його огляду відсікаються фрустумом основної камери.
// Рішення: щокадру віддзеркалюємо захоплення в ACesiumCameraManager як FCesiumCamera.
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::ResolveCesiumCameraManager()
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;
	CesiumCameraManager = ACesiumCameraManager::GetDefaultCameraManager(this);
}

void UUAVCameraComponent::SyncCesiumSceneCaptureCamera()
{
	// Дзеркалимо умови ACesium3DTileset::GetSceneCaptures: перспектива, розмірений RT, дійсний FOV.
	if (!CaptureComponent || !RenderTarget ||
		CaptureComponent->ProjectionType != ECameraProjectionMode::Perspective ||
		CaptureComponent->FOVAngle <= 0.f ||
		RenderTarget->SizeX < 1 || RenderTarget->SizeY < 1)
	{
		UnregisterCesiumSceneCaptureCamera();
		return;
	}

	if (!CesiumCameraManager.IsValid()) ResolveCesiumCameraManager();
	ACesiumCameraManager* Mgr = CesiumCameraManager.Get();
	if (!Mgr) return;

	// FOVAngle — це горизонтальні градуси — передаються напряму, точно так, як Cesium робить
	// для захоплень сцени рівня. OverrideAspectRatio лишається 0 (виводиться з розміру).
	const FCesiumCamera Cam(
		FVector2D(RenderTarget->SizeX, RenderTarget->SizeY),
		CaptureComponent->GetComponentLocation(),
		CaptureComponent->GetComponentRotation(),
		CaptureComponent->FOVAngle);

	if (!bCesiumCameraRegistered || CesiumCameraId == INDEX_NONE)
	{
		CesiumCameraId = Mgr->AddCamera(Cam);
		bCesiumCameraRegistered = true;
	}
	else if (!Mgr->UpdateCamera(CesiumCameraId, Cam))
	{
		// Менеджер перестворено (наприклад, seamless travel) або id втрачено — додаємо повторно.
		CesiumCameraId = Mgr->AddCamera(Cam);
	}
}

void UUAVCameraComponent::UnregisterCesiumSceneCaptureCamera()
{
	if (!bCesiumCameraRegistered) return;
	if (ACesiumCameraManager* Mgr = CesiumCameraManager.Get())
		Mgr->RemoveCamera(CesiumCameraId);
	CesiumCameraId = INDEX_NONE;
	bCesiumCameraRegistered = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Тік
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::SetCameraProcessingEnabled(bool bEnable)
{
	bIsProcessingEnabled = bEnable;
	SetActive(bEnable);
	SetComponentTickEnabled(bEnable);
	if (CaptureComponent)
		CaptureComponent->bCaptureEveryFrame = bEnable;

	// Тік зупиняється при вимкненні, тож камеру Cesium потрібно видалити саме тут.
	if (!bEnable)
		UnregisterCesiumSceneCaptureCamera();
}

void UUAVCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsProcessingEnabled) return;

	SyncCesiumSceneCaptureCamera();

	// Знімаємо останні закодовані результати у стабільні в межах тіку кеші перед обробкою.
	// Споживачі, що викликають GetRGBFrame() / GetMaskFrame() цього тіку, бачать узгоджений знімок.
	{
		FScopeLock Lock(&LatestRGBMutex);
		if (bHasLatestRGBFrame)
		{
			TickRGBPayload   = LatestRGBPayload;
			TickRGBTimestamp = LatestRGBTimestamp;
			bHasTickRGBFrame = true;
		}
	}
	{
		FScopeLock Lock(&LatestMaskMutex);
		if (bHasLatestMaskFrame)
		{
			TickMaskPayload   = LatestMaskPayload;
			TickMaskTimestamp = LatestMaskTimestamp;
			bHasTickMaskFrame = true;
		}
	}

	ProcessFrame();
	CaptureMask();
}

// ─────────────────────────────────────────────────────────────────────────────
// RGB-захоплення
// ─────────────────────────────────────────────────────────────────────────────

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

	if (RGBFrameReadyEvent)
	{
		const double Now = FPlatformTime::Seconds();
		if ((Now - LastRGBEncodeTime) >= MinEncodeInterval)
		{
			LastRGBEncodeTime = Now;
			{
				FScopeLock Lock(&RGBFrameMutex);
				FMemory::Memcpy(PendingRGBBGRA.GetData(), ProcessedFrameBuffer.data, CVWidth * CVHeight * 4);
				PendingRGBTimestamp = GetWorld()->GetTimeSeconds();
				bHasPendingRGBFrame = true;
			}
			RGBFrameReadyEvent->Trigger();
		}
	}

	UploadToTexture();
}

// ─────────────────────────────────────────────────────────────────────────────
// Захоплення маски — позичає CaptureComponent на один CaptureScene(), потім повертає назад.
// ReadPixels() скидає (flush) потік рендеру, тож команда CaptureScene() гарантовано
// виконається до читання пікселів — міжтіковий буферинг не потрібен. Це утримує кадр маски
// узгодженим із RGB-кадром того самого тіку.
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::CaptureMask()
{
	if (!CaptureComponent || !MaskRenderTarget || !MaskPostProcessMaterial) return;
	if (!MaskFrameReadyEvent) return;

	const double Now = FPlatformTime::Seconds();
	if ((Now - LastMaskEncodeTime) < MinEncodeInterval) return;

	// Позичаємо CaptureComponent, спрямовуємо на MaskRenderTarget, впроваджуємо матеріал пост-процесу.
	UTextureRenderTarget2D*    OriginalRT         = CaptureComponent->TextureTarget;
	TArray<FWeightedBlendable> OriginalBlendables = CaptureComponent->PostProcessSettings.WeightedBlendables.Array;

	CaptureComponent->TextureTarget = MaskRenderTarget;
	CaptureComponent->PostProcessSettings.WeightedBlendables.Array.Add(
		FWeightedBlendable(1.0f, MaskPostProcessMaterial)
	);
	CaptureComponent->CaptureScene();

	// Відновлюємо негайно — команда рендеру вже тримає власне посилання.
	CaptureComponent->TextureTarget                                 = OriginalRT;
	CaptureComponent->PostProcessSettings.WeightedBlendables.Array = OriginalBlendables;

	// ReadPixels скидає всі відкладені команди рендеру, включно з CaptureScene() вище,
	// тож MaskRenderTarget повністю заповнений до того, як ми його читаємо.
	FTextureRenderTargetResource* RTResource = MaskRenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	TArray<FColor> ColorBuffer;
	RTResource->ReadPixels(ColorBuffer);
	if (ColorBuffer.Num() != CVWidth * CVHeight) return;

	LastMaskEncodeTime = Now;
	{
		FScopeLock Lock(&MaskFrameMutex);
		FMemory::Memcpy(PendingMaskBGRA.GetData(), ColorBuffer.GetData(), CVWidth * CVHeight * 4);
		PendingMaskTimestamp = GetWorld()->GetTimeSeconds();
		bHasPendingMaskFrame = true;
	}
	MaskFrameReadyEvent->Trigger();
}

// ─────────────────────────────────────────────────────────────────────────────
// Публічні акцесори кадрів — стабільні в межах тіку, лише ігровий потік
// ─────────────────────────────────────────────────────────────────────────────

bool UUAVCameraComponent::GetRGBFrame(TArray<uint8>& OutPayload, double& OutTimestamp) const
{
	if (!bHasTickRGBFrame) return false;
	OutPayload   = TickRGBPayload;
	OutTimestamp = TickRGBTimestamp;
	return true;
}

bool UUAVCameraComponent::GetMaskFrame(TArray<uint8>& OutPayload, double& OutTimestamp) const
{
	if (!bHasTickMaskFrame) return false;
	OutPayload   = TickMaskPayload;
	OutTimestamp = TickMaskTimestamp;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Потік кодувальника RGB
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::RGBEncoderLoop()
{
	TArray<uint8> LocalBGRA;
	double        LocalTimestamp = 0.0;

	IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(EImageFormat::JPEG);

	while (bRGBEncoderRunning)
	{
		RGBFrameReadyEvent->Wait(200);
		if (!bRGBEncoderRunning) break;

		{
			FScopeLock Lock(&RGBFrameMutex);
			if (!bHasPendingRGBFrame) continue;
			Swap(LocalBGRA, PendingRGBBGRA);
			PendingRGBBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);
			LocalTimestamp      = PendingRGBTimestamp;
			bHasPendingRGBFrame = false;
		}

		if (LocalBGRA.Num() != CVWidth * CVHeight * 4) continue;

		if (!Wrapper.IsValid() || !Wrapper->SetRaw(LocalBGRA.GetData(), LocalBGRA.Num(), CVWidth, CVHeight, ERGBFormat::BGRA, 8))
			continue;

		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(JpegQuality);

		{
			FScopeLock Lock(&LatestRGBMutex);
			LatestRGBPayload.Reset();
			LatestRGBPayload.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
			LatestRGBTimestamp = LocalTimestamp;
			bHasLatestRGBFrame = true;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Потік кодувальника маски
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::MaskEncoderLoop()
{
	TArray<uint8> LocalBGRA;
	double        LocalTimestamp = 0.0;

	IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(EImageFormat::JPEG);

	while (bMaskEncoderRunning)
	{
		MaskFrameReadyEvent->Wait(200);
		if (!bMaskEncoderRunning) break;

		{
			FScopeLock Lock(&MaskFrameMutex);
			if (!bHasPendingMaskFrame) continue;
			Swap(LocalBGRA, PendingMaskBGRA);
			PendingMaskBGRA.SetNumUninitialized(CVWidth * CVHeight * 4);
			LocalTimestamp       = PendingMaskTimestamp;
			bHasPendingMaskFrame = false;
		}

		if (LocalBGRA.Num() != CVWidth * CVHeight * 4) continue;

		if (!Wrapper.IsValid() || !Wrapper->SetRaw(LocalBGRA.GetData(), LocalBGRA.Num(), CVWidth, CVHeight, ERGBFormat::BGRA, 8))
			continue;

		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(JpegQuality);

		{
			FScopeLock Lock(&LatestMaskMutex);
			LatestMaskPayload.Reset();
			LatestMaskPayload.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
			LatestMaskTimestamp = LocalTimestamp;
			bHasLatestMaskFrame = true;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Допоміжні функції
// ─────────────────────────────────────────────────────────────────────────────

void UUAVCameraComponent::ComputeFOV(float HFovDeg)
{
	HorizontalFOVDeg = HFovDeg;

	const float AspectRatio = static_cast<float>(CVWidth) / static_cast<float>(CVHeight);
	const float HFovRad     = FMath::DegreesToRadians(HFovDeg);
	VerticalFOVDeg = FMath::RadiansToDegrees(
		2.0f * FMath::Atan(FMath::Tan(HFovRad * 0.5f) / AspectRatio)
	);
}

void UUAVCameraComponent::LogCameraIntrinsics() const
{
	if (!CaptureComponent) return;

	// Фокусна відстань у пікселях, виведена з горизонтального FOV і роздільної здатності.
	// Припущення про квадратні пікселі справедливе, оскільки VerticalFOVDeg сам виводиться
	// з HorizontalFOVDeg через співвідношення сторін CVWidth/CVHeight (див. ComputeFOV).
	const float FocalPx = (CVWidth * 0.5f) / FMath::Tan(FMath::DegreesToRadians(HorizontalFOVDeg * 0.5f));

	FMatrix ProjectionMatrix;
	if (CaptureComponent->bUseCustomProjectionMatrix)
	{
		ProjectionMatrix = CaptureComponent->CustomProjectionMatrix;
	}
	else if (CaptureComponent->ProjectionType == ECameraProjectionMode::Perspective)
	{
		const float AspectRatio = static_cast<float>(CVWidth) / static_cast<float>(CVHeight);
		const float HalfFOVRad  = CaptureComponent->FOVAngle * (float)PI / 360.0f;
		ProjectionMatrix = FReversedZPerspectiveMatrix(HalfFOVRad, AspectRatio, 1.0f, GNearClippingPlane);
	}
	else
	{
		const float OrthoWidth  = CaptureComponent->OrthoWidth / 2.0f;
		const float OrthoHeight = OrthoWidth * static_cast<float>(CVHeight) / static_cast<float>(CVWidth);
		ProjectionMatrix = FReversedZOrthoMatrix(OrthoWidth, OrthoHeight, 0.5f / OrthoWidth, GNearClippingPlane);
	}

	UE_LOG(LogUAV, Log, TEXT("UAVCameraComponent [%s]: resolution=%dx%d HFOV=%.2f° VFOV=%.2f° focal=%.2fpx"),
		*GetOwner()->GetName(), CVWidth, CVHeight, HorizontalFOVDeg, VerticalFOVDeg, FocalPx);
	UE_LOG(LogUAV, Log, TEXT("UAVCameraComponent [%s]: ProjectionMatrix ="), *GetOwner()->GetName());
	UE_LOG(LogUAV, Log, TEXT("  [%.4f %.4f %.4f %.4f]"), ProjectionMatrix.M[0][0], ProjectionMatrix.M[0][1], ProjectionMatrix.M[0][2], ProjectionMatrix.M[0][3]);
	UE_LOG(LogUAV, Log, TEXT("  [%.4f %.4f %.4f %.4f]"), ProjectionMatrix.M[1][0], ProjectionMatrix.M[1][1], ProjectionMatrix.M[1][2], ProjectionMatrix.M[1][3]);
	UE_LOG(LogUAV, Log, TEXT("  [%.4f %.4f %.4f %.4f]"), ProjectionMatrix.M[2][0], ProjectionMatrix.M[2][1], ProjectionMatrix.M[2][2], ProjectionMatrix.M[2][3]);
	UE_LOG(LogUAV, Log, TEXT("  [%.4f %.4f %.4f %.4f]"), ProjectionMatrix.M[3][0], ProjectionMatrix.M[3][1], ProjectionMatrix.M[3][2], ProjectionMatrix.M[3][3]);
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
