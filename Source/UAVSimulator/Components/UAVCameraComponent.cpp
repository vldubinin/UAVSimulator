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

	// Compute FOV from SceneCaptureComponent2D's default so the editor shows
	// meaningful values before the actor is ever placed in a level.
	ComputeFOV(90.0f);
}

void UUAVCameraComponent::OnRegister()
{
	Super::OnRegister();

	// Try to read the actual FOV from the capture component. In the editor this
	// runs after native components are registered; Blueprint-added components may
	// not be here yet, so we fall back to the default (90°) when not found.
	AActor* Owner = GetOwner();
	if (Owner)
	{
		if (USceneCaptureComponent2D* Comp = Owner->FindComponentByClass<USceneCaptureComponent2D>())
			ComputeFOV(Comp->FOVAngle);
	}
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
		UE_LOG(LogUAV, Error, TEXT("UAVCameraComponent: USceneCaptureComponent2D not found on %s. Add one in the Blueprint."), *Owner->GetName());
		return;
	}

	ComputeFOV(CaptureComponent->FOVAngle);
	UE_LOG(LogUAV, Log, TEXT("UAVCameraComponent: Camera found on %s (HFOV=%.1f° VFOV=%.1f°)"),
		*Owner->GetName(), HorizontalFOVDeg, VerticalFOVDeg);

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

	CollectSceneActorsForBBox();
}

void UUAVCameraComponent::CollectSceneActorsForBBox()
{
	if (!SceneActorsForBBox.IsEmpty()) {
		return;
	}

	TArray<FHitResult> FHitResults = USensorUtilityLibrary::FindActors(
		this,                    // Контекст для отримання World
		CaptureComponent->GetComponentTransform(), // Початкова точка та ротація лідара
		GetOwner(),              // Ігноруємо сам дрон
		Range,
		HorizontalRays,
		VerticalLayers,
		VerticalFOVDeg,
		CollisionChannel
	);

	for (FHitResult Hit : FHitResults)
	{
		SceneActorsForBBox.AddUnique(Hit.GetActor());
	}
}

FBox2D UUAVCameraComponent::GetActorBBoxFromSceneCapture(AActor* ActorForBBox)
{
	FBox2D Result2DBox;
	Result2DBox.bIsValid = false;

	// Перевірка на валідність компонентів
	if (!IsValid(CaptureComponent) || !IsValid(CaptureComponent->TextureTarget) || !IsValid(ActorForBBox))
	{
		return Result2DBox;
	}

	// 1. Отримуємо розміри Render Target (зображення)
	const int32 SizeX = CaptureComponent->TextureTarget->SizeX;
	const int32 SizeY = CaptureComponent->TextureTarget->SizeY;
	const float AspectRatio = static_cast<float>(SizeX) / static_cast<float>(SizeY);

	// 2. Розраховуємо View Matrix (Матрицю Виду)
	FTransform CaptureTransform = CaptureComponent->GetComponentTransform();
	FVector ViewLocation = CaptureTransform.GetLocation();
	FRotator ViewRotation = CaptureTransform.GetRotation().Rotator();

	// Трансформація координат Unreal у координати камери
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewRotation);
	FMatrix ViewMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix *
		FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1)
		);

	// 3. Розраховуємо Projection Matrix (Матрицю Проекції)
	FMatrix ProjectionMatrix;
	if (CaptureComponent->bUseCustomProjectionMatrix)
	{
		ProjectionMatrix = CaptureComponent->CustomProjectionMatrix;
	}
	else if (CaptureComponent->ProjectionType == ECameraProjectionMode::Perspective)
	{
		float FOV = CaptureComponent->FOVAngle * (float)PI / 360.0f;
		ProjectionMatrix = FReversedZPerspectiveMatrix(
			FOV,
			AspectRatio,
			1.0f,
			GNearClippingPlane
		);
	}
	else // Orthographic
	{
		float OrthoWidth = CaptureComponent->OrthoWidth / 2.0f;
		float OrthoHeight = OrthoWidth / AspectRatio;
		ProjectionMatrix = FReversedZOrthoMatrix(
			OrthoWidth,
			OrthoHeight,
			0.5f / OrthoWidth,
			GNearClippingPlane
		);
	}

	// Фінальна матриця
	FMatrix ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;

	// 4. Отримуємо 3D BBox актора та його 8 вершин
	FBox Actor3DBox = ActorForBBox->GetComponentsBoundingBox(true);

	FVector Vertices[8] = {
		FVector(Actor3DBox.Min.X, Actor3DBox.Min.Y, Actor3DBox.Min.Z),
		FVector(Actor3DBox.Min.X, Actor3DBox.Min.Y, Actor3DBox.Max.Z),
		FVector(Actor3DBox.Min.X, Actor3DBox.Max.Y, Actor3DBox.Min.Z),
		FVector(Actor3DBox.Min.X, Actor3DBox.Max.Y, Actor3DBox.Max.Z),
		FVector(Actor3DBox.Max.X, Actor3DBox.Min.Y, Actor3DBox.Min.Z),
		FVector(Actor3DBox.Max.X, Actor3DBox.Min.Y, Actor3DBox.Max.Z),
		FVector(Actor3DBox.Max.X, Actor3DBox.Max.Y, Actor3DBox.Min.Z),
		FVector(Actor3DBox.Max.X, Actor3DBox.Max.Y, Actor3DBox.Max.Z)
	};

	// 5. Проектуємо кожну 3D вершину у 2D простір екрану
	bool bAnyVertexInFrontOfCamera = false;

	for (int32 i = 0; i < 8; ++i)
	{
		FVector4 ProjectedV = ViewProjectionMatrix.TransformFVector4(FVector4(Vertices[i], 1.f));

		// Якщо точка знаходиться перед камерою (W > 0)
		if (ProjectedV.W > 0.0f)
		{
			bAnyVertexInFrontOfCamera = true;
			float RHW = 1.0f / ProjectedV.W;

			// Конвертуємо у координати пікселів зображення [0..SizeX, 0..SizeY]
			FVector2D ScreenPos(
				(ProjectedV.X * RHW + 1.0f) * (SizeX / 2.0f),
				(1.0f - ProjectedV.Y * RHW) * (SizeY / 2.0f)
			);

			Result2DBox += ScreenPos; // += автоматично розширює Min та Max обмежувального прямокутника
		}
	}

	// 6. Обрізаємо (Clamp) BBox по краях екрану, якщо хоча б одна вершина була валідною
	if (Result2DBox.bIsValid && bAnyVertexInFrontOfCamera)
	{
		Result2DBox.Min.X = FMath::Clamp(Result2DBox.Min.X, 0.0f, static_cast<float>(SizeX));
		Result2DBox.Min.Y = FMath::Clamp(Result2DBox.Min.Y, 0.0f, static_cast<float>(SizeY));
		Result2DBox.Max.X = FMath::Clamp(Result2DBox.Max.X, 0.0f, static_cast<float>(SizeX));
		Result2DBox.Max.Y = FMath::Clamp(Result2DBox.Max.Y, 0.0f, static_cast<float>(SizeY));
	}
	else
	{
		// Якщо всі вершини позаду камери
		Result2DBox.bIsValid = false;
	}

	return Result2DBox;
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

	CollectSceneActorsForBBox();

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

	for (AActor* ActorForBBox : SceneActorsForBBox)
	{
		FBox2D Bbox = GetActorBBoxFromSceneCapture(ActorForBBox);
		BBoxs.Add(ActorForBBox->GetName(), Bbox);
	}
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

		//FString JsonString = FString::Printf(TEXT("{\"fov\": %.1f, \"status\": \"tracking\"}"), HorizontalFOVDeg);
		FString JsonString = ConvertBBoxMapToJson(BBoxs);
		FTCHARToUTF8 JsonUtf8(*JsonString);
		int32 JsonLength = JsonUtf8.Length();

		{
			FScopeLock Lock(&LatestFrameMutex);
			LatestEncodedPayload.Reset();

			// 1. Записуємо 4 байти розміру JSON
			LatestEncodedPayload.Append(reinterpret_cast<const uint8*>(&JsonLength), sizeof(int32));

			// 2. Записуємо самі байти JSON-тексту (якщо він є)
			if (JsonLength > 0)
			{
				LatestEncodedPayload.Append(reinterpret_cast<const uint8*>(JsonUtf8.Get()), JsonLength);
			}

			// 3. Записуємо байти JPEG зображення
			LatestEncodedPayload.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));

			LatestEncodedTimestamp = LocalTimestamp;
			bHasLatestFrame = true;
		}
	}
}

FString UUAVCameraComponent::ConvertBBoxMapToJson(const TMap<FString, FBox2D>& BBoxsMap)
{
	// Створюємо головний JSON об'єкт, який буде представляти нашу мапу
	TSharedPtr<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject());

	for (const auto& Pair : BBoxsMap)
	{
		// Створюємо JSON об'єкт для конкретного FBox2D
		TSharedPtr<FJsonObject> BoxJsonObject = MakeShareable(new FJsonObject());

		// Створюємо JSON об'єкт для точки Min
		TSharedPtr<FJsonObject> MinJsonObject = MakeShareable(new FJsonObject());
		MinJsonObject->SetNumberField("X", Pair.Value.Min.X);
		MinJsonObject->SetNumberField("Y", Pair.Value.Min.Y);
		BoxJsonObject->SetObjectField("Min", MinJsonObject);

		// Створюємо JSON об'єкт для точки Max
		TSharedPtr<FJsonObject> MaxJsonObject = MakeShareable(new FJsonObject());
		MaxJsonObject->SetNumberField("X", Pair.Value.Max.X);
		MaxJsonObject->SetNumberField("Y", Pair.Value.Max.Y);
		BoxJsonObject->SetObjectField("Max", MaxJsonObject);

		// Додаємо прапорець валідності (за бажанням)
		BoxJsonObject->SetBoolField("bIsValid", Pair.Value.bIsValid);

		// Додаємо цей Box у головний об'єкт під ключем з TMap (FString)
		RootJsonObject->SetObjectField(Pair.Key, BoxJsonObject);
	}

	// Змінна для збереження результату
	FString OutputJsonString;

	// Створюємо Writer для серіалізації
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJsonString);

	// Серіалізуємо JSON об'єкт у рядок
	FJsonSerializer::Serialize(RootJsonObject.ToSharedRef(), Writer);

	return OutputJsonString;
}


// ─────────────────────────────────────────────────────────────────────────────
// FOV helpers
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
