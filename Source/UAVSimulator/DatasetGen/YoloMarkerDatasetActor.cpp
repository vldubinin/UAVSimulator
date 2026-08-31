#include "YoloMarkerDatasetActor.h"
#include "UAVSimulator/UAVSimulator.h"

#include "CesiumGeoreference.h"
#include "Cesium3DTileset.h"
#include "CesiumCameraManager.h"
#include "CesiumCamera.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>
#include "PostOpenCVHeaders.h"

// The four "bbox" corner names, in winding order — identical to
// UCustomSurroundingsScannerComponent::LoadObjects so a footprint quad matches.
static const TCHAR* GCornerNames[] = { TEXT("x_min"), TEXT("x_max"), TEXT("y_min"), TEXT("y_max") };

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

AYoloMarkerDatasetActor::AYoloMarkerDatasetActor()
{
	PrimaryActorTick.bCanEverTick          = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	CaptureComp = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("YoloMarkerCapture"));
	CaptureComp->SetupAttachment(RootComponent);
	CaptureComp->bCaptureEveryFrame           = false;
	CaptureComp->bCaptureOnMovement           = false;
	CaptureComp->bAlwaysPersistRenderingState = true;
	CaptureComp->CaptureSource                = ESceneCaptureSource::SCS_FinalColorLDR;
	CaptureComp->ShowFlags.SetPostProcessing(true);

	OrbitRadiiMeters      = { 200.0f, 400.0f };
	OutputRootDir         = FPaths::ProjectSavedDir() / TEXT("YoloMarkerDataset");
	ObjectsSourceFilePath = FPaths::ProjectDir() / TEXT("Tools/TestingPlatform/attitude_control/map_objects.json");
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry points
// ─────────────────────────────────────────────────────────────────────────────

void AYoloMarkerDatasetActor::GenerateDataset()
{
	if (bRunning)
	{
		UE_LOG(LogUAV, Warning, TEXT("YoloMarkerDataset: генерація вже триває"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
		return;

	Georeference = ACesiumGeoreference::GetDefaultGeoreference(this);
	if (!Georeference)
	{
		UE_LOG(LogUAV, Warning, TEXT("YoloMarkerDataset: не знайдено ACesiumGeoreference — запускай у Play-світі"));
		return;
	}

	TArray<AActor*> Tilesets;
	UGameplayStatics::GetAllActorsOfClass(World, ACesium3DTileset::StaticClass(), Tilesets);
	Tileset = Tilesets.Num() > 0 ? Cast<ACesium3DTileset>(Tilesets[0]) : nullptr;

	// Cesium only feeds tile selection from player/editor cameras and standalone
	// ASceneCapture2D actors — never a capture component nested in an actor. Register
	// our capture pose with the camera manager so tiles under each shot are streamed.
	CesiumCameraManager = ACesiumCameraManager::GetDefaultCameraManager(this);

	if (!LoadObjects() || Objects.Num() == 0)
	{
		UE_LOG(LogUAV, Warning, TEXT("YoloMarkerDataset: не завантажено жодного маркера"));
		return;
	}

	BuildClassMap();
	BuildShots();
	if (Shots.Num() == 0)
	{
		UE_LOG(LogUAV, Warning, TEXT("YoloMarkerDataset: список пострілів порожній — перевір параметри Sweep"));
		return;
	}

	if (OutputRootDir.IsEmpty())
		OutputRootDir = FPaths::ProjectSavedDir() / TEXT("YoloMarkerDataset");

	ImagesTrainDir = OutputRootDir / TEXT("images/train");
	ImagesValDir   = OutputRootDir / TEXT("images/val");
	LabelsTrainDir = OutputRootDir / TEXT("labels/train");
	LabelsValDir   = OutputRootDir / TEXT("labels/val");
	DebugDir       = OutputRootDir / TEXT("debug");

	IFileManager& FM = IFileManager::Get();
	FM.MakeDirectory(*ImagesTrainDir, /*Tree=*/true);
	FM.MakeDirectory(*ImagesValDir,   /*Tree=*/true);
	FM.MakeDirectory(*LabelsTrainDir, /*Tree=*/true);
	FM.MakeDirectory(*LabelsValDir,   /*Tree=*/true);
	if (bSaveDebugImages)
		FM.MakeDirectory(*DebugDir, /*Tree=*/true);

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("YoloMarkerRT"));
	RenderTarget->InitCustomFormat(RenderWidth, RenderHeight, PF_B8G8R8A8, false);
	RenderTarget->UpdateResourceImmediate(true);

	CaptureComp->FOVAngle      = CameraFOV;
	CaptureComp->TextureTarget = RenderTarget;

	ShotCursor         = 0;
	SettleCounter      = 0;
	SavedFrameCount    = 0;
	AttemptedShotCount = 0;
	ManifestFrames.Reset();
	ValEveryN = FMath::Max(2, FMath::RoundToInt(1.0f / FMath::Clamp(ValSplit, 0.02f, 0.9f)));
	Rng.Initialize(1337);

	bRunning = true;
	SetActorTickEnabled(true);

	UE_LOG(LogUAV, Log, TEXT("YoloMarkerDataset: старт — %d маркерів, %d пострілів, вихід → %s"),
		Objects.Num(), Shots.Num(), *OutputRootDir);
}

void AYoloMarkerDatasetActor::CancelGeneration()
{
	if (!bRunning)
		return;

	UE_LOG(LogUAV, Warning, TEXT("YoloMarkerDataset: скасовано на постріл %d/%d"), ShotCursor, Shots.Num());
	FinishGeneration(/*bCancelled=*/true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick — one shot per frame: place → settle → capture → save → advance.
// ─────────────────────────────────────────────────────────────────────────────

void AYoloMarkerDatasetActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRunning)
		ProcessCurrentShot();
}

void AYoloMarkerDatasetActor::ProcessCurrentShot()
{
	if (!Shots.IsValidIndex(ShotCursor))
	{
		FinishGeneration(/*bCancelled=*/false);
		return;
	}

	const FShot& Shot = Shots[ShotCursor];
	FCustomSurroundingObject& Target = Objects[Shot.MarkerIdx];

	// Retried until every corner lands a tile hit — a far marker's tiles may not be
	// streamed in yet when its first shot comes up.
	if (bSnapMarkersToTileSurface && !Target.bGroundHeightResolved)
		ResolveGroundHeights(Target);

	PlaceCameraForShot(Target, Shot);

	// Feed the new pose to Cesium every tick (including during the settle wait) so the
	// tiles under it actively stream/refine rather than depending on another camera.
	SyncCesiumCaptureCamera();

	// Hold the pose for a few ticks so Cesium streams the tiles under it before we read.
	if (SettleCounter < SettleFrames)
	{
		++SettleCounter;
		return;
	}

	CaptureComp->CaptureScene();

	TArray<FColor> Pixels;
	if (FTextureRenderTargetResource* Res = RenderTarget->GameThread_GetRenderTargetResource())
		Res->ReadPixels(Pixels); // flushes the render thread

	if (Pixels.Num() == RenderWidth * RenderHeight)
	{
		TArray<FLabel> Labels;
		bool bTargetSeen = false;
		for (const FCustomSurroundingObject& Object : Objects)
		{
			FLabel Label;
			if (ComputeMarkerLabel(Object, Label))
			{
				if (Object.ObjectID == Target.ObjectID)
					bTargetSeen = true;
				Labels.Add(MoveTemp(Label));
			}
		}

		++AttemptedShotCount;

		const bool bAccept = Labels.Num() > 0 && (!bRequireTargetVisible || bTargetSeen);
		if (bAccept)
			SaveFrame(Pixels, Labels, Shot, Target.ObjectID);
	}

	++ShotCursor;
	SettleCounter = 0;

	if ((ShotCursor % 50) == 0 || ShotCursor == Shots.Num())
		UE_LOG(LogUAV, Log, TEXT("YoloMarkerDataset: %d/%d пострілів, збережено %d кадрів"),
			ShotCursor, Shots.Num(), SavedFrameCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// Marker load — same JSON schema and winding order as UCustomSurroundingsScannerComponent.
// ─────────────────────────────────────────────────────────────────────────────

bool AYoloMarkerDatasetActor::LoadObjects()
{
	Objects.Reset();

	FString Json;
	if (!ObjectsSourceFilePath.IsEmpty() && FFileHelper::LoadFileToString(Json, *ObjectsSourceFilePath))
	{
		UE_LOG(LogUAV, Log, TEXT("YoloMarkerDataset: маркери з файлу %s"), *ObjectsSourceFilePath);
	}
	else
	{
		Json = ObjectsJsonInline;
		UE_LOG(LogUAV, Log, TEXT("YoloMarkerDataset: маркери з ObjectsJsonInline (%d символів)"), Json.Len());
	}

	TArray<TSharedPtr<FJsonValue>> ParsedArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, ParsedArray))
	{
		UE_LOG(LogUAV, Warning, TEXT("YoloMarkerDataset: не вдалося розпарсити JSON маркерів"));
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : ParsedArray)
	{
		const TSharedPtr<FJsonObject>* JsonObject;
		if (!Value->TryGetObject(JsonObject))
			continue;

		FCustomSurroundingObject Entry;
		(*JsonObject)->TryGetStringField(TEXT("elementId"), Entry.ObjectID);
		(*JsonObject)->TryGetStringField(TEXT("type"), Entry.ObjectType);
		(*JsonObject)->TryGetNumberField(TEXT("altitude"), Entry.AltitudeMeters);

		if (Entry.ObjectID.IsEmpty())
			continue;
		if (!OnlyMarkerId.IsEmpty() && Entry.ObjectID != OnlyMarkerId)
			continue;

		const TSharedPtr<FJsonObject>* BBoxObject = nullptr;
		if (!(*JsonObject)->TryGetObjectField(TEXT("bbox"), BBoxObject) || !BBoxObject->IsValid())
			continue;

		double SumLat = 0.0;
		double SumLong = 0.0;
		int32  CornerCount = 0;
		Entry.BBoxCornersWorldMeters.Reset((int32)UE_ARRAY_COUNT(GCornerNames));

		for (const TCHAR* CornerName : GCornerNames)
		{
			const TSharedPtr<FJsonObject>* CornerObject = nullptr;
			if (!(*BBoxObject)->TryGetObjectField(CornerName, CornerObject) || !CornerObject->IsValid())
				continue;

			double CornerLat = 0.0;
			double CornerLong = 0.0;
			(*CornerObject)->TryGetNumberField(TEXT("latitude"), CornerLat);
			(*CornerObject)->TryGetNumberField(TEXT("longitude"), CornerLong);

			Entry.BBoxCornersWorldMeters.Add(GeoToWorldMeters(CornerLat, CornerLong, 0.0));
			SumLat  += CornerLat;
			SumLong += CornerLong;
			++CornerCount;
		}

		if (CornerCount == 0)
			continue;

		Entry.Latitude  = SumLat / CornerCount;
		Entry.Longitude = SumLong / CornerCount;

		FVector CentreWorldMeters = FVector::ZeroVector;
		for (const FVector& Corner : Entry.BBoxCornersWorldMeters)
			CentreWorldMeters += Corner;
		Entry.WorldLocationMeters = CentreWorldMeters / CornerCount;

		Objects.Add(MoveTemp(Entry));

		if (MaxMarkers > 0 && Objects.Num() >= MaxMarkers)
			break;
	}

	return Objects.Num() > 0;
}

void AYoloMarkerDatasetActor::BuildClassMap()
{
	ClassMap.Reset();
	ClassNames.Reset();

	TArray<FString> Types;
	for (const FCustomSurroundingObject& Object : Objects)
	{
		const FString Type = Object.ObjectType.IsEmpty() ? TEXT("object") : Object.ObjectType;
		Types.AddUnique(Type);
	}
	Types.Sort();

	for (int32 i = 0; i < Types.Num(); ++i)
	{
		ClassMap.Add(Types[i], i);
		ClassNames.Add(Types[i]);
	}
}

void AYoloMarkerDatasetActor::BuildShots()
{
	Shots.Reset();

	TArray<float> Radii = OrbitRadiiMeters;
	if (Radii.Num() == 0)
		Radii.Add(300.0f);

	const float ElMax = FMath::Max(ElevationMinDeg, ElevationMaxDeg);

	for (int32 MarkerIdx = 0; MarkerIdx < Objects.Num(); ++MarkerIdx)
	{
		for (float Radius : Radii)
		{
			for (float El = ElevationMinDeg; El <= ElMax + KINDA_SMALL_NUMBER; El += ElevationStep)
			{
				for (float Az = 0.0f; Az < 360.0f - KINDA_SMALL_NUMBER; Az += AzimuthStep)
				{
					Shots.Add({ MarkerIdx, Radius, Az, El });
				}
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Geo → world + ground snapping — copied from UCustomSurroundingsScannerComponent
// so the markers sit exactly where the runtime scanner would put them.
// ─────────────────────────────────────────────────────────────────────────────

FVector AYoloMarkerDatasetActor::GeoToWorldMeters(double LatitudeDeg, double LongitudeDeg, double HeightMeters) const
{
	if (!Georeference)
		return FVector::ZeroVector;

	const FVector LongitudeLatitudeHeight(LongitudeDeg, LatitudeDeg, HeightMeters);
	const FVector UnrealPositionCm = Georeference->GetActorTransform().TransformPosition(
		Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(LongitudeLatitudeHeight));
	return UnrealPositionCm * 0.01;
}

FVector AYoloMarkerDatasetActor::GeographicUpMeters(double LatitudeDeg, double LongitudeDeg) const
{
	if (!Georeference)
		return FVector::UpVector;

	const FVector Low  = GeoToWorldMeters(LatitudeDeg, LongitudeDeg, 0.0);
	const FVector High = GeoToWorldMeters(LatitudeDeg, LongitudeDeg, 1000.0);
	const FVector Up   = (High - Low).GetSafeNormal();
	return Up.IsNearlyZero() ? FVector::UpVector : Up;
}

void AYoloMarkerDatasetActor::ResolveGroundHeights(FCustomSurroundingObject& Object) const
{
	UWorld* World = GetWorld();
	const int32 CornerCount = Object.BBoxCornersWorldMeters.Num();
	if (!World || CornerCount == 0)
		return;

	const FVector UpMeters = GeographicUpMeters(Object.Latitude, Object.Longitude);
	const FVector UpDir    = UpMeters.GetSafeNormal();
	if (UpDir.IsNearlyZero())
		return;

	const double SpanCm = FMath::Max(GroundTraceSpanMeters, 1.0f) * 100.0;

	int32 HitCount = 0;
	for (FVector& Corner : Object.BBoxCornersWorldMeters)
	{
		const FVector FromCm = Corner * 100.0;
		const FVector Start  = FromCm + UpDir * SpanCm;
		const FVector End    = FromCm - UpDir * SpanCm;

		FCollisionQueryParams Params(TEXT("YoloMarkerGroundTrace"), /*bTraceComplex=*/true, this);

		TArray<FHitResult> Hits;
		World->LineTraceMultiByChannel(Hits, Start, End, GroundTraceChannel, Params);

		const FHitResult* Accepted = nullptr;
		for (const FHitResult& Hit : Hits)
		{
			const AActor* HitActor = Hit.GetActor();
			// Accept any Cesium3DTileset hit; if the scene has none, accept any blocking geometry.
			if (Tileset && (!HitActor || !HitActor->IsA<ACesium3DTileset>()))
				continue;
			Accepted = &Hit;
			break;
		}

		if (Accepted)
		{
			Corner = Accepted->ImpactPoint * 0.01;
			++HitCount;
		}
	}

	FVector CentreSum = FVector::ZeroVector;
	for (const FVector& Corner : Object.BBoxCornersWorldMeters)
		CentreSum += Corner;
	Object.WorldLocationMeters = CentreSum / CornerCount;

	Object.bGroundHeightResolved = (HitCount == CornerCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera placement — orbit in the marker's local ENU frame so azimuth/elevation
// are geographically meaningful regardless of the georeference rotation.
// ─────────────────────────────────────────────────────────────────────────────

void AYoloMarkerDatasetActor::PlaceCameraForShot(const FCustomSurroundingObject& Target, const FShot& Shot)
{
	const FVector TargetCm = Target.WorldLocationMeters * 100.0;

	const FVector Up = GeographicUpMeters(Target.Latitude, Target.Longitude);

	FVector East = FVector::CrossProduct(Up, FVector::ForwardVector);
	if (East.IsNearlyZero())
		East = FVector::CrossProduct(Up, FVector::RightVector);
	East = East.GetSafeNormal();

	const FVector North = FVector::CrossProduct(East, Up).GetSafeNormal();

	const float AzRad = FMath::DegreesToRadians(Shot.AzimuthDeg);
	const float ElRad = FMath::DegreesToRadians(Shot.ElevationDeg);

	const FVector Dir = (FMath::Cos(ElRad) * (FMath::Cos(AzRad) * North + FMath::Sin(AzRad) * East)
	                     + FMath::Sin(ElRad) * Up).GetSafeNormal();

	const FVector CamPos = TargetCm + Dir * (Shot.RadiusMeters * 100.0);
	FRotator LookRot = (TargetCm - CamPos).GetSafeNormal().Rotation();

	if (AimJitterDeg > KINDA_SMALL_NUMBER)
	{
		LookRot.Yaw   += Rng.FRandRange(-AimJitterDeg, AimJitterDeg);
		LookRot.Pitch += Rng.FRandRange(-AimJitterDeg, AimJitterDeg);
	}

	CaptureComp->SetWorldLocationAndRotation(CamPos, LookRot);
}

// ─────────────────────────────────────────────────────────────────────────────
// Projection — identical view/projection matrix setup to
// UCustomSurroundingsScannerComponent::ProjectWorldToScreenUnclamped. Takes metres.
// Returns false only when the point is behind the camera.
// ─────────────────────────────────────────────────────────────────────────────

bool AYoloMarkerDatasetActor::ProjectMetersToPixels(const FVector& WorldMeters, FVector2D& OutPixels) const
{
	if (!CaptureComp || RenderWidth <= 0 || RenderHeight <= 0)
		return false;

	const FVector WorldPositionCm = WorldMeters * 100.0;
	const float AspectRatio = static_cast<float>(RenderWidth) / static_cast<float>(RenderHeight);

	const FTransform CaptureTransform = CaptureComp->GetComponentTransform();
	const FVector    ViewLocation     = CaptureTransform.GetLocation();
	const FRotator   ViewRotation     = CaptureTransform.GetRotation().Rotator();

	FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewRotation);
	FMatrix ViewMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix *
		FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1)
		);

	FMatrix ProjectionMatrix;
	if (CaptureComp->bUseCustomProjectionMatrix)
	{
		ProjectionMatrix = CaptureComp->CustomProjectionMatrix;
	}
	else if (CaptureComp->ProjectionType == ECameraProjectionMode::Perspective)
	{
		const float FOV = CaptureComp->FOVAngle * (float)PI / 360.0f;
		ProjectionMatrix = FReversedZPerspectiveMatrix(FOV, AspectRatio, 1.0f, GNearClippingPlane);
	}
	else
	{
		const float OrthoWidth  = CaptureComp->OrthoWidth / 2.0f;
		const float OrthoHeight = OrthoWidth / AspectRatio;
		ProjectionMatrix = FReversedZOrthoMatrix(OrthoWidth, OrthoHeight, 0.5f / OrthoWidth, GNearClippingPlane);
	}

	const FMatrix  ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
	const FVector4 Projected            = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPositionCm, 1.f));

	if (Projected.W <= 0.0f)
	{
		OutPixels = FVector2D::ZeroVector;
		return false;
	}

	const float RHW     = 1.0f / Projected.W;
	const float ScreenX = (Projected.X * RHW + 1.0f) * (RenderWidth * 0.5f);
	const float ScreenY = (1.0f - Projected.Y * RHW) * (RenderHeight * 0.5f);

	OutPixels = FVector2D(ScreenX, ScreenY);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Marker → pixel bounding box. Projects the 4 footprint corners (plus 4 raised
// corners when MarkerHeightMeters > 0), builds the axis-aligned bounds, clips to
// the frame and applies the size / visibility filters.
// ─────────────────────────────────────────────────────────────────────────────

bool AYoloMarkerDatasetActor::ComputeMarkerLabel(const FCustomSurroundingObject& Object, FLabel& OutLabel) const
{
	if (Object.BBoxCornersWorldMeters.Num() < 3)
		return false;

	TArray<FVector, TInlineAllocator<8>> Points;
	for (const FVector& Corner : Object.BBoxCornersWorldMeters)
		Points.Add(Corner);

	if (MarkerHeightMeters > KINDA_SMALL_NUMBER)
	{
		const FVector Up = GeographicUpMeters(Object.Latitude, Object.Longitude) * MarkerHeightMeters;
		for (const FVector& Corner : Object.BBoxCornersWorldMeters)
			Points.Add(Corner + Up);
	}

	FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D Max(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	int32 InFront = 0;

	for (const FVector& Point : Points)
	{
		FVector2D Px;
		if (!ProjectMetersToPixels(Point, Px))
			continue;

		++InFront;
		Min.X = FMath::Min(Min.X, Px.X);
		Min.Y = FMath::Min(Min.Y, Px.Y);
		Max.X = FMath::Max(Max.X, Px.X);
		Max.Y = FMath::Max(Max.Y, Px.Y);
	}

	if (InFront < 3)
		return false;

	const float FullW = Max.X - Min.X;
	const float FullH = Max.Y - Min.Y;
	if (FullW <= 0.0f || FullH <= 0.0f)
		return false;

	const float ClipX0 = FMath::Clamp(Min.X, 0.0f, static_cast<float>(RenderWidth));
	const float ClipY0 = FMath::Clamp(Min.Y, 0.0f, static_cast<float>(RenderHeight));
	const float ClipX1 = FMath::Clamp(Max.X, 0.0f, static_cast<float>(RenderWidth));
	const float ClipY1 = FMath::Clamp(Max.Y, 0.0f, static_cast<float>(RenderHeight));

	const float ClipW = ClipX1 - ClipX0;
	const float ClipH = ClipY1 - ClipY0;
	if (ClipW <= 0.0f || ClipH <= 0.0f)
		return false;

	const float VisibleFraction = (ClipW * ClipH) / (FullW * FullH);
	if (VisibleFraction < MinVisibleFraction)
		return false;
	if (ClipW < MinBBoxPixels || ClipH < MinBBoxPixels)
		return false;

	const FString Type = Object.ObjectType.IsEmpty() ? TEXT("object") : Object.ObjectType;
	const int32*  Found = ClassMap.Find(Type);

	OutLabel.ClassId         = Found ? *Found : 0;
	OutLabel.Id              = Object.ObjectID;
	OutLabel.Type            = Type;
	OutLabel.X               = ClipX0;
	OutLabel.Y               = ClipY0;
	OutLabel.W               = ClipW;
	OutLabel.H               = ClipH;
	OutLabel.VisibleFraction = VisibleFraction;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame output — PNG + YOLO .txt (+ optional annotated debug PNG) + manifest row.
// ─────────────────────────────────────────────────────────────────────────────

bool AYoloMarkerDatasetActor::SaveFrame(const TArray<FColor>& Pixels, const TArray<FLabel>& Labels,
                                        const FShot& Shot, const FString& TargetId)
{
	const bool bVal = ValSplit > 0.0f && (SavedFrameCount % ValEveryN) == 0;
	const FString Split    = bVal ? TEXT("val") : TEXT("train");
	const FString BaseName = FString::Printf(TEXT("frame_%06d"), SavedFrameCount);

	const FString ImagePath = (bVal ? ImagesValDir : ImagesTrainDir) / (BaseName + TEXT(".png"));
	const FString LabelPath = (bVal ? LabelsValDir : LabelsTrainDir) / (BaseName + TEXT(".txt"));

	// FColor is B,G,R,A in memory — CV_8UC4 + BGRA2BGR is correct.
	cv::Mat BGRA(RenderHeight, RenderWidth, CV_8UC4,
		const_cast<void*>(static_cast<const void*>(Pixels.GetData())));
	cv::Mat BGR;
	cv::cvtColor(BGRA, BGR, cv::COLOR_BGRA2BGR);

	if (!cv::imwrite(TCHAR_TO_UTF8(*ImagePath), BGR))
	{
		UE_LOG(LogUAV, Warning, TEXT("YoloMarkerDataset: не вдалося записати %s"), *ImagePath);
		return false;
	}

	// ── YOLO label file: "<class> <cx> <cy> <w> <h>" normalised to [0,1] ──────
	FString LabelText;
	for (const FLabel& Label : Labels)
	{
		const float Cx = (Label.X + Label.W * 0.5f) / RenderWidth;
		const float Cy = (Label.Y + Label.H * 0.5f) / RenderHeight;
		const float W  = Label.W / RenderWidth;
		const float H  = Label.H / RenderHeight;
		LabelText += FString::Printf(TEXT("%d %.6f %.6f %.6f %.6f\n"), Label.ClassId, Cx, Cy, W, H);
	}
	FFileHelper::SaveStringToFile(LabelText, *LabelPath);

	// ── Optional annotated debug image ──────────────────────────────────────
	if (bSaveDebugImages)
	{
		cv::Mat Dbg = BGR.clone();
		for (const FLabel& Label : Labels)
		{
			const cv::Rect Box(FMath::RoundToInt(Label.X), FMath::RoundToInt(Label.Y),
			                   FMath::RoundToInt(Label.W), FMath::RoundToInt(Label.H));
			const bool bIsTarget = Label.Id == TargetId;
			const cv::Scalar Colour = bIsTarget ? cv::Scalar(0, 200, 255) : cv::Scalar(0, 220, 0);
			cv::rectangle(Dbg, Box, Colour, bIsTarget ? 3 : 2, cv::LINE_AA);

			const std::string Text = TCHAR_TO_UTF8(*FString::Printf(TEXT("%s"), *Label.Id));
			cv::putText(Dbg, Text, cv::Point(Box.x + 2, FMath::Max(12, Box.y - 4)),
				cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
			cv::putText(Dbg, Text, cv::Point(Box.x + 2, FMath::Max(12, Box.y - 4)),
				cv::FONT_HERSHEY_SIMPLEX, 0.45, Colour, 1, cv::LINE_AA);
		}
		const std::string Head = TCHAR_TO_UTF8(*FString::Printf(
			TEXT("target=%s  r=%.0fm  az=%.0f  el=%.0f"), *TargetId, Shot.RadiusMeters, Shot.AzimuthDeg, Shot.ElevationDeg));
		cv::putText(Dbg, Head, cv::Point(8, 20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
		cv::putText(Dbg, Head, cv::Point(8, 20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

		cv::imwrite(TCHAR_TO_UTF8(*(DebugDir / (BaseName + TEXT(".png")))), Dbg);
	}

	// ── Manifest row ────────────────────────────────────────────────────────
	TSharedRef<FJsonObject> FrameObj = MakeShared<FJsonObject>();
	FrameObj->SetStringField(TEXT("image"),         Split / (BaseName + TEXT(".png")));
	FrameObj->SetStringField(TEXT("split"),         Split);
	FrameObj->SetStringField(TEXT("target_id"),     TargetId);
	FrameObj->SetNumberField(TEXT("radius_m"),      Shot.RadiusMeters);
	FrameObj->SetNumberField(TEXT("azimuth_deg"),   Shot.AzimuthDeg);
	FrameObj->SetNumberField(TEXT("elevation_deg"), Shot.ElevationDeg);

	TArray<TSharedPtr<FJsonValue>> ObjectsJson;
	for (const FLabel& Label : Labels)
	{
		TSharedRef<FJsonObject> ObjJson = MakeShared<FJsonObject>();
		ObjJson->SetStringField(TEXT("id"),       Label.Id);
		ObjJson->SetStringField(TEXT("type"),     Label.Type);
		ObjJson->SetNumberField(TEXT("class_id"), Label.ClassId);

		TArray<TSharedPtr<FJsonValue>> BoxPx;
		BoxPx.Add(MakeShared<FJsonValueNumber>(Label.X));
		BoxPx.Add(MakeShared<FJsonValueNumber>(Label.Y));
		BoxPx.Add(MakeShared<FJsonValueNumber>(Label.W));
		BoxPx.Add(MakeShared<FJsonValueNumber>(Label.H));
		ObjJson->SetArrayField(TEXT("bbox_px"), BoxPx);

		TArray<TSharedPtr<FJsonValue>> BoxYolo;
		BoxYolo.Add(MakeShared<FJsonValueNumber>((Label.X + Label.W * 0.5f) / RenderWidth));
		BoxYolo.Add(MakeShared<FJsonValueNumber>((Label.Y + Label.H * 0.5f) / RenderHeight));
		BoxYolo.Add(MakeShared<FJsonValueNumber>(Label.W / RenderWidth));
		BoxYolo.Add(MakeShared<FJsonValueNumber>(Label.H / RenderHeight));
		ObjJson->SetArrayField(TEXT("bbox_yolo"), BoxYolo);

		ObjJson->SetNumberField(TEXT("visible_fraction"), Label.VisibleFraction);
		ObjectsJson.Add(MakeShared<FJsonValueObject>(ObjJson));
	}
	FrameObj->SetArrayField(TEXT("objects"), ObjectsJson);
	ManifestFrames.Add(MakeShared<FJsonValueObject>(FrameObj));

	++SavedFrameCount;
	return true;
}

void AYoloMarkerDatasetActor::WriteDatasetYaml() const
{
	FString Yaml;
	Yaml += FString::Printf(TEXT("path: %s\n"), *FPaths::ConvertRelativePathToFull(OutputRootDir).Replace(TEXT("\\"), TEXT("/")));
	Yaml += TEXT("train: images/train\n");
	Yaml += TEXT("val: images/val\n");
	Yaml += FString::Printf(TEXT("nc: %d\n"), ClassNames.Num());
	Yaml += TEXT("names:\n");
	for (int32 i = 0; i < ClassNames.Num(); ++i)
		Yaml += FString::Printf(TEXT("  %d: %s\n"), i, *ClassNames[i]);

	FFileHelper::SaveStringToFile(Yaml, *(OutputRootDir / TEXT("data.yaml")));
}

void AYoloMarkerDatasetActor::WriteManifest() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("dataset"),          TEXT("yolo-marker-detection"));
	Root->SetNumberField(TEXT("image_width"),      RenderWidth);
	Root->SetNumberField(TEXT("image_height"),     RenderHeight);
	Root->SetNumberField(TEXT("saved_frames"),     SavedFrameCount);
	Root->SetNumberField(TEXT("attempted_shots"),  AttemptedShotCount);
	Root->SetNumberField(TEXT("planned_shots"),    Shots.Num());
	Root->SetNumberField(TEXT("marker_count"),     Objects.Num());

	TArray<TSharedPtr<FJsonValue>> ClassesJson;
	for (int32 i = 0; i < ClassNames.Num(); ++i)
	{
		TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetNumberField(TEXT("id"), i);
		C->SetStringField(TEXT("name"), ClassNames[i]);
		ClassesJson.Add(MakeShared<FJsonValueObject>(C));
	}
	Root->SetArrayField(TEXT("classes"), ClassesJson);
	Root->SetArrayField(TEXT("frames"), ManifestFrames);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(Output, *(OutputRootDir / TEXT("dataset.json")));
}

void AYoloMarkerDatasetActor::FinishGeneration(bool bCancelled)
{
	WriteDatasetYaml();
	WriteManifest();

	bRunning = false;
	SetActorTickEnabled(false);
	UnregisterCesiumCaptureCamera();
	CaptureComp->TextureTarget = nullptr;
	RenderTarget = nullptr;

	UE_LOG(LogUAV, Log, TEXT("YoloMarkerDataset: %s — збережено %d кадрів з %d спроб (%d заплановано) → %s"),
		bCancelled ? TEXT("СКАСОВАНО") : TEXT("ГОТОВО"),
		SavedFrameCount, AttemptedShotCount, Shots.Num(), *OutputRootDir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cesium camera registration — see GenerateDataset() for why this is needed.
// Mirrors ACesium3DTileset::GetSceneCaptures gating (perspective, sized RT, valid FOV)
// and the stable-id ACesiumCameraManager API used by AAirplane's onboard camera.
// ─────────────────────────────────────────────────────────────────────────────

void AYoloMarkerDatasetActor::SyncCesiumCaptureCamera()
{
	if (!CaptureComp || !RenderTarget ||
		CaptureComp->ProjectionType != ECameraProjectionMode::Perspective ||
		CaptureComp->FOVAngle <= 0.f ||
		RenderTarget->SizeX < 1 || RenderTarget->SizeY < 1)
		return;

	if (!CesiumCameraManager.IsValid())
		CesiumCameraManager = ACesiumCameraManager::GetDefaultCameraManager(this);
	ACesiumCameraManager* Mgr = CesiumCameraManager.Get();
	if (!Mgr)
		return;

	// FOVAngle is horizontal degrees — passed straight through, exactly as Cesium does
	// for level scene captures. OverrideAspectRatio stays 0 (derived from ViewportSize).
	const FCesiumCamera Cam(
		FVector2D(RenderTarget->SizeX, RenderTarget->SizeY),
		CaptureComp->GetComponentLocation(),
		CaptureComp->GetComponentRotation(),
		CaptureComp->FOVAngle);

	if (CesiumCameraId == INDEX_NONE)
		CesiumCameraId = Mgr->AddCamera(Cam);
	else if (!Mgr->UpdateCamera(CesiumCameraId, Cam))
		CesiumCameraId = Mgr->AddCamera(Cam); // manager recreated / id lost — re-add
}

void AYoloMarkerDatasetActor::UnregisterCesiumCaptureCamera()
{
	if (CesiumCameraId == INDEX_NONE)
		return;
	if (ACesiumCameraManager* Mgr = CesiumCameraManager.Get())
		Mgr->RemoveCamera(CesiumCameraId);
	CesiumCameraId = INDEX_NONE;
}
