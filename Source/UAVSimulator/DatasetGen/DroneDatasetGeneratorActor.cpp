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
// Construction
// ─────────────────────────────────────────────────────────────────────────────

ADroneDatasetGeneratorActor::ADroneDatasetGeneratorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// APawn does not set a root automatically; create one so USceneComponent
	// children can attach (matches the pattern noted in CLAUDE.md).
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
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────

void ADroneDatasetGeneratorActor::GenerateDataset()
{
	if (!DroneBlueprintClass)
	{
		UE_LOG(LogUAV, Warning, TEXT("DatasetGenerator: DroneBlueprintClass is not set."));
		return;
	}
	if (OutputJsonPath.IsEmpty())
	{
		UE_LOG(LogUAV, Warning, TEXT("DatasetGenerator: OutputJsonPath is empty."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// ── Spawn drone ───────────────────────────────────────────────────────────
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Drone = World->SpawnActor<AActor>(
		DroneBlueprintClass, GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!Drone)
	{
		UE_LOG(LogUAV, Error, TEXT("DatasetGenerator: Failed to spawn drone Blueprint."));
		return;
	}

	// Enable Custom Depth on every primitive so the mask PP material sees the drone.
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

	// ── Image output directory ────────────────────────────────────────────────
	FString ImageDir;
	if (bSaveDebugImages)
	{
		ImageDir = OutputImageDir.IsEmpty()
			? FPaths::Combine(FPaths::GetPath(OutputJsonPath),
			                  FPaths::GetBaseFilename(OutputJsonPath) + TEXT("_frames"))
			: OutputImageDir;

		IFileManager::Get().MakeDirectory(*ImageDir, /*CreateTree=*/true);
		UE_LOG(LogUAV, Log, TEXT("DatasetGenerator: Debug images → %s"), *ImageDir);
	}

	// ── Sweep ─────────────────────────────────────────────────────────────────
	const FVector DroneOrigin = Drone->GetActorLocation();
	TArray<FFrameData> Frames;

	UE_LOG(LogUAV, Log, TEXT("DatasetGenerator: Starting sweep for %s"),
		*DroneBlueprintClass->GetName());

	int FrameId = 0;

	// Captures one camera position and appends a frame if pixels are valid.
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

	// South pole — single frame, azimuth irrelevant.
	CaptureFrame(0.f, -90.f);

	// Latitude rings from −90+step to 90−step.
	for (float Elev = -90.f + ElevationStep; Elev < 90.f - KINDA_SMALL_NUMBER; Elev += ElevationStep)
		for (float Azim = 0.f; Azim < 360.f - KINDA_SMALL_NUMBER; Azim += AzimuthStep)
			CaptureFrame(Azim, Elev);

	// North pole — single frame.
	CaptureFrame(0.f, 90.f);

	// ── Cleanup drone ─────────────────────────────────────────────────────────
	Drone->Destroy();

	// ── Write JSON ────────────────────────────────────────────────────────────
	const FString ModelName = DroneBlueprintClass->GetName();
	const FString JsonStr   = BuildJson(ModelName, Frames);

	if (FFileHelper::SaveStringToFile(JsonStr, *OutputJsonPath))
	{
		UE_LOG(LogUAV, Log, TEXT("DatasetGenerator: Saved %d frames → %s"),
			Frames.Num(), *OutputJsonPath);
	}
	else
	{
		UE_LOG(LogUAV, Error, TEXT("DatasetGenerator: Failed to write %s"), *OutputJsonPath);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera placement
// ─────────────────────────────────────────────────────────────────────────────

void ADroneDatasetGeneratorActor::PlaceCameraAt(
	const FVector& Target, float AzimuthDeg, float ElevationDeg)
{
	const float AzRad = FMath::DegreesToRadians(AzimuthDeg);
	const float ElRad = FMath::DegreesToRadians(ElevationDeg);

	// Spherical → Cartesian (UE axes: X forward, Y right, Z up)
	const float X = OrbitRadius * FMath::Cos(ElRad) * FMath::Cos(AzRad);
	const float Y = OrbitRadius * FMath::Cos(ElRad) * FMath::Sin(AzRad);
	const float Z = OrbitRadius * FMath::Sin(ElRad);

	const FVector  CamPos  = Target + FVector(X, Y, Z);
	const FRotator LookRot = (Target - CamPos).GetSafeNormal().Rotation();

	CaptureComp->SetWorldLocationAndRotation(CamPos, LookRot);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mask capture — show-only strategy
//
// Switches the capture to PRM_UseShowOnlyList with only the drone actor so the
// scene renders as "drone against a guaranteed-black RT clear color". This works
// without any post-process material and is immune to custom-depth show-flag
// issues that caused the original full-scene bleed-through.
//
// If MaskPostProcessMaterial is set it is injected on top (optional effect).
// ReadPixels() flushes the render thread before we read pixel data.
// ─────────────────────────────────────────────────────────────────────────────

bool ADroneDatasetGeneratorActor::CaptureMask(
	AActor* DroneActor, UTextureRenderTarget2D* RT, TArray<FColor>& OutPixels)
{
	if (!CaptureComp || !RT || !DroneActor) return false;

	// ── Save state ────────────────────────────────────────────────────────────
	const ESceneCapturePrimitiveRenderMode OrigMode   = CaptureComp->PrimitiveRenderMode;
	TArray<AActor*>                        OrigShow   = CaptureComp->ShowOnlyActors;
	UTextureRenderTarget2D*                OrigRT     = CaptureComp->TextureTarget;
	TArray<FWeightedBlendable>             OrigBl     = CaptureComp->PostProcessSettings.WeightedBlendables.Array;

	// ── Capture: drone only against black ────────────────────────────────────
	CaptureComp->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	CaptureComp->ShowOnlyActors      = { DroneActor };
	CaptureComp->TextureTarget       = RT;

	if (MaskPostProcessMaterial)
	{
		CaptureComp->PostProcessSettings.WeightedBlendables.Array.Add(
			FWeightedBlendable(1.0f, MaskPostProcessMaterial));
	}

	CaptureComp->CaptureScene();

	// ── Restore before flush so the render command keeps its own refs ─────────
	CaptureComp->PrimitiveRenderMode                             = OrigMode;
	CaptureComp->ShowOnlyActors                                  = OrigShow;
	CaptureComp->TextureTarget                                   = OrigRT;
	CaptureComp->PostProcessSettings.WeightedBlendables.Array   = OrigBl;

	FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
	if (!Res) return false;

	Res->ReadPixels(OutPixels);  // flushes render thread
	return OutPixels.Num() == RenderWidth * RenderHeight;
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenCV contour extraction
// ─────────────────────────────────────────────────────────────────────────────

TArray<FVector2D> ADroneDatasetGeneratorActor::ExtractPolygon(
	const TArray<FColor>& Pixels) const
{
	// FColor is laid out as B, G, R, A in memory — CV_8UC4 + BGRA2GRAY is correct.
	cv::Mat BGRA(RenderHeight, RenderWidth, CV_8UC4,
		const_cast<void*>(static_cast<const void*>(Pixels.GetData())));

	cv::Mat Gray;
	cv::cvtColor(BGRA, Gray, cv::COLOR_BGRA2GRAY);

	// Blur — kernel must be odd; clamp to [1, 31].
	const int BlurK = FMath::Clamp(SilhouetteBlurSize | 1, 1, 31);
	if (BlurK > 1)
		cv::GaussianBlur(Gray, Gray, cv::Size(BlurK, BlurK), 0);

	// Otsu finds the optimal threshold between the black background and the drone.
	cv::Mat Binary;
	cv::threshold(Gray, Binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

	// Close: merge thin protrusions (landing gear, antennas) into the main body.
	const int CloseK = FMath::Max(FillGapsSize | 1, 1);
	const cv::Mat CloseKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(CloseK, CloseK));
	cv::morphologyEx(Binary, Binary, cv::MORPH_CLOSE, CloseKernel);

	// Open: erase isolated blobs that survived the close pass.
	const int OpenK = FMath::Max(RemoveIslandsSize | 1, 1);
	const cv::Mat OpenKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(OpenK, OpenK));
	cv::morphologyEx(Binary, Binary, cv::MORPH_OPEN, OpenKernel);

	std::vector<std::vector<cv::Point>> Contours;
	cv::findContours(Binary, Contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

	if (Contours.empty()) return {};

	// Keep the largest contour (the drone body).
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
// Debug image — mask + polygon overlay saved as PNG
// ─────────────────────────────────────────────────────────────────────────────

void ADroneDatasetGeneratorActor::SaveDebugImage(
	const TArray<FColor>& Pixels,
	const TArray<FVector2D>& Polygon,
	float AzimuthDeg, float ElevationDeg,
	const FString& ImageDir, int FrameId) const
{
	// BGRA (FColor memory layout) → BGR for imwrite
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

		// Semi-transparent green fill
		cv::Mat Overlay = BGR.clone();
		cv::fillPoly(Overlay, ContourVec, cv::Scalar(0, 200, 0));
		cv::addWeighted(BGR, 0.65, Overlay, 0.35, 0.0, BGR);

		// Solid outline
		cv::polylines(BGR, ContourVec, /*isClosed=*/true,
			cv::Scalar(0, 255, 0), 2, cv::LINE_AA);

		// Vertex dots — small red filled circles
		for (const cv::Point& Pt : Pts)
			cv::circle(BGR, Pt, 2, cv::Scalar(0, 0, 255), cv::FILLED, cv::LINE_AA);
	}

	// Label: camera position. Dark thick pass first, then bright thin — readable on both backgrounds.
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
// JSON serialisation
// ─────────────────────────────────────────────────────────────────────────────

FString ADroneDatasetGeneratorActor::BuildJson(
	const FString& ModelName, const TArray<FFrameData>& Frames) const
{
	// ── Neighbor lookup ───────────────────────────────────────────────────────
	// Key: "Az_El" (rounded to nearest degree). Poles always use Az=0.
	TMap<FString, int32> FrameLookup;
	FrameLookup.Reserve(Frames.Num());

	auto MakeKey = [](float Az, float El) -> FString
	{
		return FString::Printf(TEXT("%.0f_%.0f"), Az, El);
	};

	for (int32 i = 0; i < Frames.Num(); ++i)
		FrameLookup.Add(MakeKey(Frames[i].Azimuth, Frames[i].Elevation), i);

	// Actual top/bottom ring elevations — ceiling arithmetic handles the case
	// where ElevationStep does not evenly divide 90.
	const int32 NRings       = FMath::CeilToInt(180.f / ElevationStep) - 1;
	const float TopRingEl    = -90.f + NRings * ElevationStep;
	const float BottomRingEl = -90.f + ElevationStep;

	// Returns the frame index for the neighbor at (dAz, dEl) from F.
	// Azimuth wraps [0, 360). Going past ±90 elevation reaches the pole (Az=0).
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

	// 8 compass directions: (name, dAzimuth, dElevation).
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

	// ── Serialize ─────────────────────────────────────────────────────────────
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
			// Pole frame: all frames in the adjacent latitude ring are neighbors.
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
			// Regular frame: up to 8 directional neighbors, deduplicated by ID.
			// Near-pole frames have multiple directions that resolve to the same
			// pole frame; only the first matching direction is kept.
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
