#include "DroneDatasetGeneratorActor.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Конструювання
// ─────────────────────────────────────────────────────────────────────────────

ADroneDatasetGeneratorActor::ADroneDatasetGeneratorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// APawn не встановлює root автоматично; створюємо його, щоб дочірні
	// USceneComponent могли приєднатися (відповідає патерну з CLAUDE.md).
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	CaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("DatasetCapture"));
	CaptureComp->SetupAttachment(Root);
	CaptureComp->bCaptureEveryFrame            = false;
	CaptureComp->bCaptureOnMovement            = false;
	CaptureComp->bAlwaysPersistRenderingState  = true;
	CaptureComp->CaptureSource                 = ESceneCaptureSource::SCS_FinalColorLDR;
	CaptureComp->ShowFlags.SetPostProcessing(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Публічна точка входу
// ─────────────────────────────────────────────────────────────────────────────

void ADroneDatasetGeneratorActor::GenerateDataset()
{
	if (!DroneBlueprintClass)
	{
		return;
	}
	if (OutputJsonPath.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// ── Спавн дрона ───────────────────────────────────────────────────────────
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Drone = World->SpawnActor<AActor>(
		DroneBlueprintClass, GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!Drone)
	{
		return;
	}

	// Вмикаємо Custom Depth на кожному примітиві, щоб маска PP-матеріалу бачила дрон.
	TArray<UPrimitiveComponent*> Prims;
	Drone->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* Prim : Prims)
	{
		Prim->SetRenderCustomDepth(true);
		Prim->SetCustomDepthStencilValue(1);
	}

	// ── Render target ─────────────────────────────────────────────────────────
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this, TEXT("DatasetRT"));
	RT->InitCustomFormat(RenderWidth, RenderHeight, PF_B8G8R8A8, false);
	RT->UpdateResourceImmediate(true);

	CaptureComp->FOVAngle = CameraFOV;

	// ── Директорія для вихідних зображень ────────────────────────────────────
	FString ImageDir;
	if (bSaveDebugImages)
	{
		ImageDir = OutputImageDir.IsEmpty()
			? FPaths::Combine(FPaths::GetPath(OutputJsonPath),
			                  FPaths::GetBaseFilename(OutputJsonPath) + TEXT("_frames"))
			: OutputImageDir;

		IFileManager::Get().MakeDirectory(*ImageDir, /*CreateTree=*/true);
	}

	// ── Обхід ─────────────────────────────────────────────────────────────────
	const FVector DroneOrigin = Drone->GetActorLocation();
	TArray<FFrameData> Frames;

	int FrameId = 0;

	// Захоплює одну позицію камери та додає кадр, якщо пікселі валідні.
	auto CaptureFrame = [&](float Azim, float Elev)
	{
		PlaceCameraAt(DroneOrigin, Azim, Elev);
		TArray<FColor> Pixels;
		if (CaptureMask(Drone, RT, Pixels))
		{
			TArray<FVector2D> Polygon = ExtractPolygon(Pixels);
			if (bSaveDebugImages)
				SaveDebugImage(Pixels, Polygon, Azim, Elev, ImageDir, FrameId++);
			Frames.Add({ Azim, Elev, MoveTemp(Polygon) });
		}
	};

	// Південний полюс — один кадр, азимут не має значення.
	CaptureFrame(0.f, -90.f);

	// Кільця широти від −90+step до 90−step.
	for (float Elev = -90.f + ElevationStep; Elev < 90.f - KINDA_SMALL_NUMBER; Elev += ElevationStep)
		for (float Azim = 0.f; Azim < 360.f - KINDA_SMALL_NUMBER; Azim += AzimuthStep)
			CaptureFrame(Azim, Elev);

	// Північний полюс — один кадр.
	CaptureFrame(0.f, 90.f);

	// ── Видалення дрона ───────────────────────────────────────────────────────
	Drone->Destroy();

	// ── Запис JSON ────────────────────────────────────────────────────────────
	const FString ModelName = DroneBlueprintClass->GetName();
	const FString JsonStr   = BuildJson(ModelName, Frames);

	FFileHelper::SaveStringToFile(JsonStr, *OutputJsonPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// Розташування камери
// ─────────────────────────────────────────────────────────────────────────────

void ADroneDatasetGeneratorActor::PlaceCameraAt(
	const FVector& Target, float AzimuthDeg, float ElevationDeg)
{
	const float AzRad = FMath::DegreesToRadians(AzimuthDeg);
	const float ElRad = FMath::DegreesToRadians(ElevationDeg);

	// Сферичні → декартові координати (осі UE: X вперед, Y вправо, Z вгору)
	const float X = OrbitRadius * FMath::Cos(ElRad) * FMath::Cos(AzRad);
	const float Y = OrbitRadius * FMath::Cos(ElRad) * FMath::Sin(AzRad);
	const float Z = OrbitRadius * FMath::Sin(ElRad);

	const FVector  CamPos  = Target + FVector(X, Y, Z);
	const FRotator LookRot = (Target - CamPos).GetSafeNormal().Rotation();

	CaptureComp->SetWorldLocationAndRotation(CamPos, LookRot);
}

// ─────────────────────────────────────────────────────────────────────────────
// Захоплення маски — стратегія show-only
//
// Перемикає захоплення в PRM_UseShowOnlyList лише з actor'ом дрона, щоб сцена
// рендерилась як "дрон на гарантовано чорному clear color рендер-таргету". Це працює
// без жодного post-process матеріалу і несприйнятливе до проблем із show-flag
// custom depth, які раніше спричиняли протікання всієї сцени.
//
// Якщо задано MaskPostProcessMaterial, він додатково інжектується поверх (опційний ефект).
// ReadPixels() робить flush рендер-потоку перед читанням даних пікселів.
// ─────────────────────────────────────────────────────────────────────────────

bool ADroneDatasetGeneratorActor::CaptureMask(
	AActor* DroneActor, UTextureRenderTarget2D* RT, TArray<FColor>& OutPixels)
{
	if (!CaptureComp || !RT || !DroneActor) return false;

	// ── Збереження стану ──────────────────────────────────────────────────────
	const ESceneCapturePrimitiveRenderMode OrigMode   = CaptureComp->PrimitiveRenderMode;
	TArray<AActor*>                        OrigShow   = CaptureComp->ShowOnlyActors;
	UTextureRenderTarget2D*                OrigRT     = CaptureComp->TextureTarget;
	TArray<FWeightedBlendable>             OrigBl     = CaptureComp->PostProcessSettings.WeightedBlendables.Array;

	// ── Захоплення: лише дрон на чорному фоні ────────────────────────────────
	CaptureComp->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	CaptureComp->ShowOnlyActors      = { DroneActor };
	CaptureComp->TextureTarget       = RT;

	if (MaskPostProcessMaterial)
	{
		CaptureComp->PostProcessSettings.WeightedBlendables.Array.Add(
			FWeightedBlendable(1.0f, MaskPostProcessMaterial));
	}

	CaptureComp->CaptureScene();

	// ── Відновлення перед flush, щоб render-команда зберігала власні посилання ─
	CaptureComp->PrimitiveRenderMode                             = OrigMode;
	CaptureComp->ShowOnlyActors                                  = OrigShow;
	CaptureComp->TextureTarget                                   = OrigRT;
	CaptureComp->PostProcessSettings.WeightedBlendables.Array   = OrigBl;

	FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
	if (!Res) return false;

	Res->ReadPixels(OutPixels);  // виконує flush рендер-потоку
	return OutPixels.Num() == RenderWidth * RenderHeight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Витягування контуру за допомогою OpenCV
// ─────────────────────────────────────────────────────────────────────────────

TArray<FVector2D> ADroneDatasetGeneratorActor::ExtractPolygon(
	const TArray<FColor>& Pixels) const
{
	// FColor у пам'яті має порядок B, G, R, A — тому CV_8UC4 + BGRA2GRAY коректні.
	cv::Mat BGRA(RenderHeight, RenderWidth, CV_8UC4,
		const_cast<void*>(static_cast<const void*>(Pixels.GetData())));

	cv::Mat Gray;
	cv::cvtColor(BGRA, Gray, cv::COLOR_BGRA2GRAY);

	// Розмиття — ядро має бути непарним; обмежуємо до [1, 31].
	const int BlurK = FMath::Clamp(SilhouetteBlurSize | 1, 1, 31);
	if (BlurK > 1)
		cv::GaussianBlur(Gray, Gray, cv::Size(BlurK, BlurK), 0);

	// Otsu знаходить оптимальний поріг між чорним фоном і дроном.
	cv::Mat Binary;
	cv::threshold(Gray, Binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

	// Closing: об'єднує тонкі виступи (шасі, антени) з основним тілом.
	const int CloseK = FMath::Max(FillGapsSize | 1, 1);
	const cv::Mat CloseKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(CloseK, CloseK));
	cv::morphologyEx(Binary, Binary, cv::MORPH_CLOSE, CloseKernel);

	// Opening: стирає ізольовані плями, що залишились після closing.
	const int OpenK = FMath::Max(RemoveIslandsSize | 1, 1);
	const cv::Mat OpenKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(OpenK, OpenK));
	cv::morphologyEx(Binary, Binary, cv::MORPH_OPEN, OpenKernel);

	std::vector<std::vector<cv::Point>> Contours;
	cv::findContours(Binary, Contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

	if (Contours.empty()) return {};

	// Залишаємо найбільший контур (тіло дрона).
	int    LargestIdx  = 0;
	double LargestArea = 0.0;
	for (int i = 0; i < static_cast<int>(Contours.size()); ++i)
	{
		const double Area = cv::contourArea(Contours[i]);
		if (Area > LargestArea) { LargestArea = Area; LargestIdx = i; }
	}

	const double Perimeter = cv::arcLength(Contours[LargestIdx], true);
	const double Epsilon   = PolygonSmoothness * Perimeter;

	std::vector<cv::Point> Approx;
	cv::approxPolyDP(Contours[LargestIdx], Approx, Epsilon, true);

	TArray<FVector2D> Result;
	Result.Reserve(static_cast<int32>(Approx.size()));
	for (const cv::Point& Pt : Approx)
		Result.Add(FVector2D(Pt.x, Pt.y));

	return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug-зображення — маска + накладений полігон, збережені як PNG
// ─────────────────────────────────────────────────────────────────────────────

void ADroneDatasetGeneratorActor::SaveDebugImage(
	const TArray<FColor>& Pixels,
	const TArray<FVector2D>& Polygon,
	float AzimuthDeg, float ElevationDeg,
	const FString& ImageDir, int FrameId) const
{
	// BGRA (розкладка пам'яті FColor) → BGR для imwrite
	cv::Mat BGRA(RenderHeight, RenderWidth, CV_8UC4,
		const_cast<void*>(static_cast<const void*>(Pixels.GetData())));
	cv::Mat BGR;
	cv::cvtColor(BGRA, BGR, cv::COLOR_BGRA2BGR);

	if (Polygon.Num() >= 2)
	{
		std::vector<cv::Point> Pts;
		Pts.reserve(Polygon.Num());
		for (const FVector2D& P : Polygon)
			Pts.push_back(cv::Point(FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y)));

		const std::vector<std::vector<cv::Point>> ContourVec = { Pts };

		// Напівпрозора зелена заливка
		cv::Mat Overlay = BGR.clone();
		cv::fillPoly(Overlay, ContourVec, cv::Scalar(0, 200, 0));
		cv::addWeighted(BGR, 0.65, Overlay, 0.35, 0.0, BGR);

		// Суцільний контур
		cv::polylines(BGR, ContourVec, /*isClosed=*/true,
			cv::Scalar(0, 255, 0), 2, cv::LINE_AA);

		// Точки вершин — маленькі закрашені червоні кола
		for (const cv::Point& Pt : Pts)
			cv::circle(BGR, Pt, 2, cv::Scalar(0, 0, 255), cv::FILLED, cv::LINE_AA);
	}

	// Підпис: позиція камери. Спочатку темний товстий прохід, потім яскравий тонкий — читабельно на обох фонах.
	const cv::Point Pos(8, 20);
	const std::string Label = TCHAR_TO_UTF8(*FString::Printf(TEXT("Az: %.0f  El: %.0f"), AzimuthDeg, ElevationDeg));
	cv::putText(BGR, Label, Pos, cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0,0,0),       3, cv::LINE_AA);
	cv::putText(BGR, Label, Pos, cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(255,255,255), 1, cv::LINE_AA);

	const FString FullPath = FPaths::Combine(ImageDir,
		FString::Printf(TEXT("%04d__az%03d_el%d.png"),
			FrameId,
			FMath::RoundToInt(AzimuthDeg), FMath::RoundToInt(ElevationDeg)));
	cv::imwrite(TCHAR_TO_UTF8(*FullPath), BGR);
}

// ─────────────────────────────────────────────────────────────────────────────
// Серіалізація в JSON
// ─────────────────────────────────────────────────────────────────────────────

FString ADroneDatasetGeneratorActor::BuildJson(
	const FString& ModelName, const TArray<FFrameData>& Frames) const
{
	// ── Пошук сусідів ─────────────────────────────────────────────────────────
	// Ключ: "Az_El" (округлено до найближчого градуса). Для полюсів завжди Az=0.
	TMap<FString, int32> FrameLookup;
	FrameLookup.Reserve(Frames.Num());

	auto MakeKey = [](float Az, float El) -> FString
	{
		return FString::Printf(TEXT("%.0f_%.0f"), Az, El);
	};

	for (int32 i = 0; i < Frames.Num(); ++i)
		FrameLookup.Add(MakeKey(Frames[i].Azimuth, Frames[i].Elevation), i);

	// Фактичні кути підвищення верхнього/нижнього кільця — арифметика зі стелею обробляє випадок,
	// коли ElevationStep не ділить 90 без остачі.
	const int32 NRings       = FMath::CeilToInt(180.f / ElevationStep) - 1;
	const float TopRingEl    = -90.f + NRings * ElevationStep;
	const float BottomRingEl = -90.f + ElevationStep;

	// Повертає індекс кадру-сусіда за (dAz, dEl) відносно F.
	// Азимут циклічний [0, 360). Вихід за межі ±90 за кутом підвищення означає полюс (Az=0).
	auto FindNeighbor = [&](const FFrameData& F, float dAz, float dEl) -> int32
	{
		float NeighEl = F.Elevation + dEl;
		float NeighAz = F.Azimuth + dAz;

		if (NeighEl >= 90.f - KINDA_SMALL_NUMBER)
		{
			NeighEl = 90.f;
			NeighAz = 0.f;
		}
		else if (NeighEl <= -90.f + KINDA_SMALL_NUMBER)
		{
			NeighEl = -90.f;
			NeighAz = 0.f;
		}
		else
		{
			NeighAz = FMath::Fmod(NeighAz + 360.f, 360.f);
		}

		const int32* Found = FrameLookup.Find(MakeKey(NeighAz, NeighEl));
		return Found ? *Found : -1;
	};

	auto IsPole = [](float El) { return FMath::IsNearlyEqual(FMath::Abs(El), 90.f, 0.5f); };

	// 8 напрямків компаса: (назва, dAzimuth, dElevation).
	const float dAz = AzimuthStep;
	const float dEl = ElevationStep;
	struct FDirection { float DAz; float DEl; };
	const FDirection Directions[] = {
		{  0.f,  +dEl },
		{ +dAz,  +dEl },
		{ +dAz,   0.f },
		{ +dAz,  -dEl },
		{  0.f,  -dEl },
		{ -dAz,  -dEl },
		{ -dAz,   0.f },
		{ -dAz,  +dEl },
	};

	// ── Серіалізація ──────────────────────────────────────────────────────────
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("drone_model"), ModelName);

	TArray<TSharedPtr<FJsonValue>> FrameArray;
	for (int32 i = 0; i < Frames.Num(); ++i)
	{
		const FFrameData& F = Frames[i];
		TSharedPtr<FJsonObject> FrameObj = MakeShared<FJsonObject>();
		FrameObj->SetNumberField(TEXT("id"),        i);
		FrameObj->SetNumberField(TEXT("azimuth"),   F.Azimuth);
		FrameObj->SetNumberField(TEXT("elevation"), F.Elevation);

		TArray<TSharedPtr<FJsonValue>> NeighborArray;

		if (IsPole(F.Elevation))
		{
			// Кадр полюса: усі кадри сусіднього кільця широти є сусідами.
			const float AdjacentEl = F.Elevation > 0.f ? TopRingEl : BottomRingEl;
			for (int32 j = 0; j < Frames.Num(); ++j)
			{
				if (FMath::IsNearlyEqual(Frames[j].Elevation, AdjacentEl, 0.5f))
				{
					NeighborArray.Add(MakeShared<FJsonValueNumber>(j));
				}
			}
		}
		else
		{
			// Звичайний кадр: до 8 напрямкових сусідів, з дедуплікацією за ID.
			// У кадрів поблизу полюса кілька напрямків можуть вести до того самого
			// кадру полюса; зберігається лише перший відповідний напрямок.
			TSet<int32> Seen;
			for (const FDirection& Dir : Directions)
			{
				const int32 NId = FindNeighbor(F, Dir.DAz, Dir.DEl);
				if (NId >= 0 && !Seen.Contains(NId))
				{
					Seen.Add(NId);
					NeighborArray.Add(MakeShared<FJsonValueNumber>(NId));
				}
			}
		}

		FrameObj->SetArrayField(TEXT("neighbors"), NeighborArray);

		TArray<TSharedPtr<FJsonValue>> PolyArray;
		for (const FVector2D& Pt : F.Polygon)
		{
			TArray<TSharedPtr<FJsonValue>> PtArray;
			PtArray.Add(MakeShared<FJsonValueNumber>(Pt.X));
			PtArray.Add(MakeShared<FJsonValueNumber>(Pt.Y));
			PolyArray.Add(MakeShared<FJsonValueArray>(PtArray));
		}
		FrameObj->SetArrayField(TEXT("polygon"), PolyArray);
		FrameArray.Add(MakeShared<FJsonValueObject>(FrameObj));
	}
	Root->SetArrayField(TEXT("frames"), FrameArray);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
