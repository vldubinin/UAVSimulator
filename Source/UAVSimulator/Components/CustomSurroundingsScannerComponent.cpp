#include "CustomSurroundingsScannerComponent.h"
#include "UAVSimulator/UAVSimulator.h"

#include "CesiumGeoreference.h"

#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

// Placeholder for a future file/network-backed source — hardcoded here for now, per the sample
// schema: [{"elementId": "...", "type": "...", "latitude": ..., "longitude": ..., "altitude": ...,
// "bboxw": ..., "bboxh": ...}]. bboxw/bboxh are optional — missing entries keep
// FCustomSurroundingObject's struct defaults.
static const TCHAR* DefaultCustomObjectsJson = TEXT(R"([
	{
		"elementId": "obj1",
		"type": "building",
		"latitude": 50.5656742,
		"longitude": 31.1472513,
		"altitude": 350,
		"bboxw": 20,
		"bboxh": 15
	},
	{
		"elementId": "obj2",
		"type": "tree",
		"latitude": 50.5656221,
		"longitude": 31.1472122,
		"altitude": 350,
		"bboxw": 6,
		"bboxh": 8
	}
])");

UCustomSurroundingsScannerComponent::UCustomSurroundingsScannerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	ObjectsJson = DefaultCustomObjectsJson;
}

void UCustomSurroundingsScannerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		SceneCaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	}

	// Fixed bbox reference orientation — captured once here, on the camera's first frame, and
	// never updated again (see InitialCameraRightAxis/InitialCameraUpAxis).
	if (SceneCaptureComponent)
	{
		const FTransform CaptureTransform = SceneCaptureComponent->GetComponentTransform();
		InitialCameraRightAxis = CaptureTransform.GetUnitAxis(EAxis::Y);
		InitialCameraUpAxis    = CaptureTransform.GetUnitAxis(EAxis::Z);
	}

	Georeference = ACesiumGeoreference::GetDefaultGeoreference(this);

	LoadObjects();
	LastLoadedObjectsJson = ObjectsJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Live-reload: pick up edits to ObjectsJson (positions, type, bboxw/bboxh, ...) without
	// requiring a restart. Scan() below re-syncs already-visible ObjectStorage entries from the
	// freshly reloaded AllObjects.
	if (ObjectsJson != LastLoadedObjectsJson)
	{
		LoadObjects();
		LastLoadedObjectsJson = ObjectsJson;
	}

	UpdateSensorSize();
	Scan();
	BuildSensorFrame();
}

// ─────────────────────────────────────────────────────────────────────────────
// Load — (re)parses ObjectsJson, converting each entry's geographic position to a world-space
// position via Georeference. Called from BeginPlay and again from TickComponent whenever
// ObjectsJson changes (see LastLoadedObjectsJson); DistanceMeters is additionally refreshed every
// Scan().
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::LoadObjects()
{
	AllObjects.Reset();

	TArray<TSharedPtr<FJsonValue>> ParsedArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ObjectsJson);
	if (!FJsonSerializer::Deserialize(Reader, ParsedArray))
	{
		UE_LOG(LogUAV, Warning, TEXT("CustomSurroundingsScanner: не вдалося розпарсити ObjectsJson"));
		return;
	}

	for (const TSharedPtr<FJsonValue>& Value : ParsedArray)
	{
		const TSharedPtr<FJsonObject>* JsonObject;
		if (!Value->TryGetObject(JsonObject)) continue;

		FCustomSurroundingObject Entry;
		(*JsonObject)->TryGetStringField(TEXT("elementId"), Entry.ObjectID);
		(*JsonObject)->TryGetStringField(TEXT("type"), Entry.ObjectType);
		(*JsonObject)->TryGetNumberField(TEXT("latitude"), Entry.Latitude);
		(*JsonObject)->TryGetNumberField(TEXT("longitude"), Entry.Longitude);
		(*JsonObject)->TryGetNumberField(TEXT("altitude"), Entry.AltitudeMeters);
		(*JsonObject)->TryGetNumberField(TEXT("bboxw"), Entry.BBoxWidthMeters);
		(*JsonObject)->TryGetNumberField(TEXT("bboxh"), Entry.BBoxHeightMeters);

		if (Entry.ObjectID.IsEmpty()) continue;

		if (Georeference)
		{
			const FVector LongitudeLatitudeHeight(Entry.Longitude, Entry.Latitude, Entry.AltitudeMeters);
			const FVector UnrealPositionCm = Georeference->GetActorTransform().TransformPosition(
				Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(LongitudeLatitudeHeight));
			Entry.WorldLocationMeters = UnrealPositionCm * 0.01;
		}

		AllObjects.Add(MoveTemp(Entry));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ObjectStorage — the only two operations that mutate it.
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::AddObject(const FString& Key, const FCustomSurroundingObject& Entry)
{
	ObjectStorage.Add(Key, Entry);

	const AActor* Owner = GetOwner();
	UE_LOG(LogUAV, Log, TEXT("CustomSurroundingsScanner: у полі зору з'явився %s (%s) — %.1f м від %s"),
		*Entry.ObjectID, *Entry.ObjectType, Entry.DistanceMeters, Owner ? *Owner->GetName() : TEXT("?"));
}

void UCustomSurroundingsScannerComponent::RemoveObject(const FString& Key)
{
	ObjectStorage.Remove(Key);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan — re-tests every loaded object against the camera's current range/frame, reconciles the
// result against ObjectStorage (add newly-visible, remove no-longer-visible), then rebuilds
// LatestScanResults and the debug rays from ObjectStorage.
// ─────────────────────────────────────────────────────────────────────────────

const TArray<FCustomSurroundingObject>& UCustomSurroundingsScannerComponent::Scan()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || !SceneCaptureComponent)
	{
		LatestScanResults.Reset();
		return LatestScanResults;
	}

	const FVector OwnerLocationMeters = Owner->GetActorLocation() * 0.01;

	TSet<FString> CurrentlyVisibleKeys;
	for (FCustomSurroundingObject& Object : AllObjects)
	{
		Object.DistanceMeters = FVector::Dist(OwnerLocationMeters, Object.WorldLocationMeters);

		FVector2D ScreenPos;
		const bool bInRange = Object.DistanceMeters <= ScanRadiusMeters;
		const bool bVisible = bInRange && ProjectWorldToScreen(Object.WorldLocationMeters * 100.0, ScreenPos);
		if (!bVisible) continue;

		CurrentlyVisibleKeys.Add(Object.ObjectID);
		// Full overwrite (not just DistanceMeters) so a live ObjectsJson edit — position, type,
		// bboxw/bboxh — reaches an already-visible object immediately, not only newly-added ones.
		if (FCustomSurroundingObject* Stored = ObjectStorage.Find(Object.ObjectID))
			*Stored = Object;
		else
			AddObject(Object.ObjectID, Object);
	}

	TArray<FString> KeysNoLongerVisible;
	for (const TPair<FString, FCustomSurroundingObject>& Pair : ObjectStorage)
		if (!CurrentlyVisibleKeys.Contains(Pair.Key))
			KeysNoLongerVisible.Add(Pair.Key);
	for (const FString& Key : KeysNoLongerVisible)
		RemoveObject(Key);

	LatestScanResults.Reset();
	LatestScanResults.Reserve(ObjectStorage.Num());
	for (const TPair<FString, FCustomSurroundingObject>& Pair : ObjectStorage)
		LatestScanResults.Add(Pair.Value);

	// Debug markers switch off together with the component — only drawn while bSensorEnabled.
	if (bSensorEnabled)
	{
		if (bDrawScanArea && SensorSizeX > 0 && SensorSizeY > 0)
		{
			const float AspectRatio = static_cast<float>(SensorSizeX) / static_cast<float>(SensorSizeY);
			const float HalfHFovRad = FMath::DegreesToRadians(SceneCaptureComponent->FOVAngle * 0.5f);
			const float HalfVFovRad = FMath::Atan(FMath::Tan(HalfHFovRad) / AspectRatio);
			DrawScanAreaDebug(SceneCaptureComponent->GetComponentTransform(), ScanRadiusMeters * 100.0f, HalfHFovRad, HalfVFovRad);
		}

		// One ray (+ optional bbox) per visible object, redrawn fresh every tick (LifeTime -1.f,
		// same single-frame-refresh convention as UCesiumSurroundingsScannerComponent's rays).
		for (const TPair<FString, FCustomSurroundingObject>& Pair : ObjectStorage)
		{
			DrawDebugLine(World, Owner->GetActorLocation(), Pair.Value.WorldLocationMeters * 100.0, RayDebugColor, false, -1.0f);

			if (bDrawObjectBBox)
				DrawObjectBBoxDebug(Pair.Value.WorldLocationMeters * 100.0, Pair.Value.BBoxWidthMeters, Pair.Value.BBoxHeightMeters);

			if (bDrawObjectLabel)
				DrawObjectLabelDebug(Pair.Value.WorldLocationMeters * 100.0, Pair.Value.ObjectID);
		}
	}

	return LatestScanResults;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan-area wireframe — a cheap 8-line frustum outline showing the camera's current view out to
// ScanRadiusMeters. Same shape/logic as UCesiumSurroundingsScannerComponent::DrawScanAreaDebug.
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::DrawScanAreaDebug(const FTransform& OriginTransform, float Range, float HalfHFovRad, float HalfVFovRad) const
{
	UWorld* World = GetWorld();
	if (!World) return;

	auto DirAt = [&OriginTransform](float HAngleRad, float VAngleRad) -> FVector
	{
		const FVector LocalDir(1.0f, FMath::Tan(HAngleRad), FMath::Tan(VAngleRad));
		return OriginTransform.TransformVectorNoScale(LocalDir).GetSafeNormal();
	};

	const FVector Origin = OriginTransform.GetLocation();
	const FVector TopLeft     = Origin + DirAt(-HalfHFovRad, HalfVFovRad) * Range;
	const FVector TopRight    = Origin + DirAt(+HalfHFovRad, HalfVFovRad) * Range;
	const FVector BottomLeft  = Origin + DirAt(-HalfHFovRad, -HalfVFovRad) * Range;
	const FVector BottomRight = Origin + DirAt(+HalfHFovRad, -HalfVFovRad) * Range;

	// Four edges from the origin to the far corners.
	DrawDebugLine(World, Origin, TopLeft,     ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, Origin, TopRight,    ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, Origin, BottomLeft,  ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, Origin, BottomRight, ScanAreaDebugColor, false, -1.0f);

	// Far rectangle connecting the four corners.
	DrawDebugLine(World, TopLeft,     TopRight,    ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, TopRight,    BottomRight, ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, BottomRight, BottomLeft,  ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, BottomLeft,  TopLeft,     ScanAreaDebugColor, false, -1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Object bbox — a rectangle around the object's point, sized from its BBoxWidthMeters/
// BBoxHeightMeters (see FCustomSurroundingObject, populated from "bboxw"/"bboxh"). Held flat
// against InitialCameraRightAxis/InitialCameraUpAxis (fixed at BeginPlay) rather than billboarded
// to the camera's current transform, so it translates with its object but never rotates.
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::ComputeBBoxCorners(const FVector& WorldPositionCm, float WidthMeters, float HeightMeters, FVector (&OutCorners)[4]) const
{
	const float HalfWidthCm  = WidthMeters * 100.0f * 0.5f;
	const float HalfHeightCm = HeightMeters * 100.0f * 0.5f;

	// TopLeft, TopRight, BottomLeft, BottomRight.
	OutCorners[0] = WorldPositionCm + InitialCameraUpAxis * HalfHeightCm - InitialCameraRightAxis * HalfWidthCm;
	OutCorners[1] = WorldPositionCm + InitialCameraUpAxis * HalfHeightCm + InitialCameraRightAxis * HalfWidthCm;
	OutCorners[2] = WorldPositionCm - InitialCameraUpAxis * HalfHeightCm - InitialCameraRightAxis * HalfWidthCm;
	OutCorners[3] = WorldPositionCm - InitialCameraUpAxis * HalfHeightCm + InitialCameraRightAxis * HalfWidthCm;
}

void UCustomSurroundingsScannerComponent::DrawObjectBBoxDebug(const FVector& WorldPositionCm, float WidthMeters, float HeightMeters) const
{
	UWorld* World = GetWorld();
	if (!World) return;

	FVector Corners[4];
	ComputeBBoxCorners(WorldPositionCm, WidthMeters, HeightMeters, Corners);
	const FVector& TopLeft     = Corners[0];
	const FVector& TopRight    = Corners[1];
	const FVector& BottomLeft  = Corners[2];
	const FVector& BottomRight = Corners[3];

	DrawDebugLine(World, TopLeft,     TopRight,    BBoxDebugColor, false, -1.0f, SDPG_Foreground);
	DrawDebugLine(World, TopRight,    BottomRight, BBoxDebugColor, false, -1.0f, SDPG_Foreground);
	DrawDebugLine(World, BottomRight, BottomLeft,  BBoxDebugColor, false, -1.0f, SDPG_Foreground);
	DrawDebugLine(World, BottomLeft,  TopLeft,     BBoxDebugColor, false, -1.0f, SDPG_Foreground);
}

// ─────────────────────────────────────────────────────────────────────────────
// Object bbox — projected screen size. Reuses ComputeBBoxCorners so the reported pixel size
// matches exactly what DrawObjectBBoxDebug draws in the world.
// ─────────────────────────────────────────────────────────────────────────────

FVector2D UCustomSurroundingsScannerComponent::ComputeBBoxScreenSize(const FVector& WorldPositionCm, float WidthMeters, float HeightMeters) const
{
	FVector Corners[4];
	ComputeBBoxCorners(WorldPositionCm, WidthMeters, HeightMeters, Corners);

	FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D Max(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	bool bAnyCornerInFront = false;

	for (const FVector& Corner : Corners)
	{
		FVector2D ScreenPos;
		if (!ProjectWorldToScreenUnclamped(Corner, ScreenPos)) continue; // behind camera — skip

		bAnyCornerInFront = true;
		Min.X = FMath::Min(Min.X, ScreenPos.X);
		Min.Y = FMath::Min(Min.Y, ScreenPos.Y);
		Max.X = FMath::Max(Max.X, ScreenPos.X);
		Max.Y = FMath::Max(Max.Y, ScreenPos.Y);
	}

	if (!bAnyCornerInFront) return FVector2D::ZeroVector;
	return FVector2D(Max.X - Min.X, Max.Y - Min.Y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Object label — the "elementId" text, drawn just above the object's point.
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::DrawObjectLabelDebug(const FVector& WorldPositionCm, const FString& Label) const
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Duration 0 → drawn for the current frame only, same redrawn-every-tick convention as the
	// other debug markers (which use LifeTime -1.0f for the same effect on lines/points).
	DrawDebugString(World, WorldPositionCm + FVector::UpVector * 50.0f, Label, nullptr, LabelDebugColor, 0.0f, false, LabelFontScale);
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface
// ─────────────────────────────────────────────────────────────────────────────

bool UCustomSurroundingsScannerComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bHasFrame) return false;

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestTimestamp;
	OutFrame.Payload   = LatestPayload;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sensor payload — one JSON object per currently-visible object (ObjectStorage), re-projected
// onto the camera every tick since the camera (not the static object) is what moves.
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::UpdateSensorSize()
{
	if (SceneCaptureComponent && (SensorSizeX <= 0 || SensorSizeY <= 0) && SceneCaptureComponent->TextureTarget)
	{
		SensorSizeX = SceneCaptureComponent->TextureTarget->SizeX;
		SensorSizeY = SceneCaptureComponent->TextureTarget->SizeY;
	}
}

void UCustomSurroundingsScannerComponent::BuildSensorFrame()
{
	if (!bSensorEnabled || !SceneCaptureComponent)
	{
		bHasFrame = false;
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ObjectsArrayJson;
	ObjectsArrayJson.Reserve(ObjectStorage.Num());

	for (const TPair<FString, FCustomSurroundingObject>& Pair : ObjectStorage)
	{
		const FCustomSurroundingObject& Entry = Pair.Value;

		FVector2D ScreenPos;
		const bool bVisible = ProjectWorldToScreen(Entry.WorldLocationMeters * 100.0, ScreenPos);

		// Pixel-space bbox size — the projected width/height of the world-space bbox
		// (BBoxWidthMeters/BBoxHeightMeters), same projection as pixel_x/pixel_y below.
		const FVector2D BBoxScreenSize = ComputeBBoxScreenSize(Entry.WorldLocationMeters * 100.0, Entry.BBoxWidthMeters, Entry.BBoxHeightMeters);

		TSharedRef<FJsonObject> ObjectJson = MakeShared<FJsonObject>();
		ObjectJson->SetStringField(TEXT("id"),        Entry.ObjectID);
		ObjectJson->SetStringField(TEXT("type"),      Entry.ObjectType);
		ObjectJson->SetNumberField(TEXT("latitude"),  Entry.Latitude);
		ObjectJson->SetNumberField(TEXT("longitude"), Entry.Longitude);
		ObjectJson->SetNumberField(TEXT("altitude"),  Entry.AltitudeMeters);
		ObjectJson->SetNumberField(TEXT("bboxw"),     BBoxScreenSize.X);
		ObjectJson->SetNumberField(TEXT("bboxh"),     BBoxScreenSize.Y);
		ObjectJson->SetNumberField(TEXT("pixel_x"),   bVisible ? ScreenPos.X : -1.0);
		ObjectJson->SetNumberField(TEXT("pixel_y"),   bVisible ? ScreenPos.Y : -1.0);
		ObjectJson->SetBoolField  (TEXT("visible"),   bVisible);
		ObjectsArrayJson.Add(MakeShared<FJsonValueObject>(ObjectJson));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("objects"), ObjectsArrayJson);

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);

	FTCHARToUTF8 JsonUtf8(*Json);
	LatestPayload.Reset();
	LatestPayload.Append(reinterpret_cast<const uint8*>(JsonUtf8.Get()), JsonUtf8.Length());
	LatestTimestamp = GetWorld()->GetTimeSeconds();
	bHasFrame       = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Projection — identical view/projection setup to
// UCesiumSurroundingsScannerComponent::ProjectWorldToScreen.
// ─────────────────────────────────────────────────────────────────────────────

bool UCustomSurroundingsScannerComponent::ProjectWorldToScreen(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const
{
	if (!ProjectWorldToScreenUnclamped(WorldPositionCm, OutScreenPos)) return false;

	return OutScreenPos.X >= 0.0f && OutScreenPos.X <= static_cast<float>(SensorSizeX) &&
	       OutScreenPos.Y >= 0.0f && OutScreenPos.Y <= static_cast<float>(SensorSizeY);
}

bool UCustomSurroundingsScannerComponent::ProjectWorldToScreenUnclamped(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const
{
	if (!SceneCaptureComponent || SensorSizeX <= 0 || SensorSizeY <= 0) return false;

	const float AspectRatio = static_cast<float>(SensorSizeX) / static_cast<float>(SensorSizeY);

	const FTransform CaptureTransform = SceneCaptureComponent->GetComponentTransform();
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
	if (SceneCaptureComponent->bUseCustomProjectionMatrix)
	{
		ProjectionMatrix = SceneCaptureComponent->CustomProjectionMatrix;
	}
	else if (SceneCaptureComponent->ProjectionType == ECameraProjectionMode::Perspective)
	{
		const float FOV = SceneCaptureComponent->FOVAngle * (float)PI / 360.0f;
		ProjectionMatrix = FReversedZPerspectiveMatrix(FOV, AspectRatio, 1.0f, GNearClippingPlane);
	}
	else
	{
		const float OrthoWidth  = SceneCaptureComponent->OrthoWidth / 2.0f;
		const float OrthoHeight = OrthoWidth / AspectRatio;
		ProjectionMatrix = FReversedZOrthoMatrix(OrthoWidth, OrthoHeight, 0.5f / OrthoWidth, GNearClippingPlane);
	}

	const FMatrix  ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
	const FVector4 Projected            = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPositionCm, 1.f));

	if (Projected.W <= 0.0f)
	{
		OutScreenPos = FVector2D::ZeroVector;
		return false;
	}

	const float RHW     = 1.0f / Projected.W;
	const float ScreenX = (Projected.X * RHW + 1.0f) * (SensorSizeX * 0.5f);
	const float ScreenY = (1.0f - Projected.Y * RHW) * (SensorSizeY * 0.5f);

	OutScreenPos = FVector2D(ScreenX, ScreenY);
	return true;
}
