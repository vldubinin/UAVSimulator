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

// Placeholder for a future file/network-backed source — hardcoded here for now, per the schema in
// Tools/TestingPlatform/attitude_control/map_objects.json: each entry is
// {"elementId": "...", "type": "...", "altitude": ..., "bbox": {x_min, x_max, y_min, y_max}} where
// every corner is a {"latitude": ..., "longitude": ...} pair. An entry with no usable "bbox" is
// skipped.
static const TCHAR* DefaultCustomObjectsJson = TEXT(R"([
	{
		"elementId": "obj3",
		"type": "building",
		"bbox": {
			"x_min": { "latitude": 50.40947022784372, "longitude": 30.61229469147912 },
			"x_max": { "latitude": 50.409468518480935, "longitude": 30.612734573757564 },
			"y_min": { "latitude": 50.40974970782978, "longitude": 30.612750667011653 },
			"y_max": { "latitude": 50.40975483588752, "longitude": 30.612314808046733 }
		},
		"altitude": 350
	},
	{
		"elementId": "obj4",
		"type": "building",
		"bbox": {
			"x_min": { "latitude": 50.4087514353538, "longitude": 30.61182999876729 },
			"x_max": { "latitude": 50.4087428884096, "longitude": 30.612311455285464 },
			"y_min": { "latitude": 50.409078782156165, "longitude": 30.61183336467758 },
			"y_max": { "latitude": 50.409075363402295, "longitude": 30.612311455285464 }
		},
		"altitude": 350
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

	// Live-reload: pick up edits to ObjectsJson (bbox corners, type, altitude, ...) without
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
// Load — (re)parses ObjectsJson, converting every entry's four "bbox" corners to world space via
// Georeference (BBoxCornersWorldMeters) and taking their mean as the footprint centre
// (Latitude/Longitude/WorldLocationMeters). Called from BeginPlay and again from TickComponent
// whenever ObjectsJson changes (see LastLoadedObjectsJson); DistanceMeters is additionally
// refreshed every Scan().
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

	// Geographic (lat, long, alt) -> world-space position in metres, via Georeference. Matches the
	// lat/long/height -> Unreal conversion used elsewhere in the project.
	auto GeoToWorldMeters = [this](double Latitude, double Longitude, double AltitudeMeters) -> FVector
	{
		if (!Georeference) return FVector::ZeroVector;
		const FVector LongitudeLatitudeHeight(Longitude, Latitude, AltitudeMeters);
		const FVector UnrealPositionCm = Georeference->GetActorTransform().TransformPosition(
			Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(LongitudeLatitudeHeight));
		return UnrealPositionCm * 0.01;
	};

	// The four "bbox" corner names, in winding order — the quad is drawn / projected in this order.
	static const TCHAR* CornerNames[] = { TEXT("x_min"), TEXT("x_max"), TEXT("y_min"), TEXT("y_max") };

	for (const TSharedPtr<FJsonValue>& Value : ParsedArray)
	{
		const TSharedPtr<FJsonObject>* JsonObject;
		if (!Value->TryGetObject(JsonObject)) continue;

		FCustomSurroundingObject Entry;
		(*JsonObject)->TryGetStringField(TEXT("elementId"), Entry.ObjectID);
		(*JsonObject)->TryGetStringField(TEXT("type"), Entry.ObjectType);
		(*JsonObject)->TryGetNumberField(TEXT("altitude"), Entry.AltitudeMeters);

		if (Entry.ObjectID.IsEmpty()) continue;

		const TSharedPtr<FJsonObject>* BBoxObject = nullptr;
		if (!(*JsonObject)->TryGetObjectField(TEXT("bbox"), BBoxObject) || !BBoxObject->IsValid())
		{
			UE_LOG(LogUAV, Warning, TEXT("CustomSurroundingsScanner: у об'єкта %s немає поля \"bbox\" — пропущено"), *Entry.ObjectID);
			continue;
		}

		double SumLat = 0.0;
		double SumLong = 0.0;
		int32  CornerCount = 0;
		Entry.BBoxCornersWorldMeters.Reset((int32)UE_ARRAY_COUNT(CornerNames));

		for (const TCHAR* CornerName : CornerNames)
		{
			const TSharedPtr<FJsonObject>* CornerObject = nullptr;
			if (!(*BBoxObject)->TryGetObjectField(CornerName, CornerObject) || !CornerObject->IsValid())
				continue;

			double CornerLat = 0.0;
			double CornerLong = 0.0;
			(*CornerObject)->TryGetNumberField(TEXT("latitude"), CornerLat);
			(*CornerObject)->TryGetNumberField(TEXT("longitude"), CornerLong);

			// "altitude" is a single value shared by every corner of the footprint.
			Entry.BBoxCornersWorldMeters.Add(GeoToWorldMeters(CornerLat, CornerLong, Entry.AltitudeMeters));

			SumLat  += CornerLat;
			SumLong += CornerLong;
			++CornerCount;
		}

		if (CornerCount == 0)
		{
			UE_LOG(LogUAV, Warning, TEXT("CustomSurroundingsScanner: у об'єкта %s порожній \"bbox\" — пропущено"), *Entry.ObjectID);
			continue;
		}

		// Footprint centre — mean of the parsed corners, in both geographic and world space.
		Entry.Latitude  = SumLat / CornerCount;
		Entry.Longitude = SumLong / CornerCount;

		FVector CentreWorldMeters = FVector::ZeroVector;
		for (const FVector& Corner : Entry.BBoxCornersWorldMeters)
			CentreWorldMeters += Corner;
		Entry.WorldLocationMeters = CentreWorldMeters / CornerCount;

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
		// Full overwrite (not just DistanceMeters) so a live ObjectsJson edit — bbox corners, type,
		// altitude — reaches an already-visible object immediately, not only newly-added ones.
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
				DrawObjectBBoxDebug(Pair.Value.BBoxCornersWorldMeters);

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
// Object bbox — the object's footprint quad, its four "bbox" corners (BBoxCornersWorldMeters,
// winding order x_min -> x_max -> y_min -> y_max) joined edge to edge. These are real world-space
// points, so the quad sits on the actual footprint — no billboarding, no fixed reference axes.
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::DrawObjectBBoxDebug(const TArray<FVector>& CornersMeters) const
{
	UWorld* World = GetWorld();
	if (!World || CornersMeters.Num() < 2) return;

	const int32 Num = CornersMeters.Num();
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FVector From = CornersMeters[Index] * 100.0;
		const FVector To   = CornersMeters[(Index + 1) % Num] * 100.0;
		DrawDebugLine(World, From, To, BBoxDebugColor, false, -1.0f, SDPG_Foreground);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Object bbox — projected screen size. Projects the same four corners DrawObjectBBoxDebug draws,
// so the reported pixel size matches the world quad exactly.
// ─────────────────────────────────────────────────────────────────────────────

FVector2D UCustomSurroundingsScannerComponent::ComputeBBoxScreenSize(const TArray<FVector>& CornersMeters) const
{
	FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D Max(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	bool bAnyCornerInFront = false;

	for (const FVector& CornerMeters : CornersMeters)
	{
		FVector2D ScreenPos;
		if (!ProjectWorldToScreenUnclamped(CornerMeters * 100.0, ScreenPos)) continue; // behind camera — skip

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

		// Pixel-space bbox size — the projected axis-aligned bounds of the footprint quad's four
		// corners (BBoxCornersWorldMeters), same projection as pixel_x/pixel_y below.
		const FVector2D BBoxScreenSize = ComputeBBoxScreenSize(Entry.BBoxCornersWorldMeters);

		// Each footprint corner projected onto the frame, unclamped, as a flat [x0,y0,x1,y1,...]
		// array in winding order x_min -> x_max -> y_min -> y_max — lets the consumer build a tight
		// axis-aligned OR an oriented (rotated) box, which is the natural fit for a footprint quad
		// seen from an oblique heading. A corner behind the camera is written as [-1, -1].
		TArray<TSharedPtr<FJsonValue>> CornersJson;
		CornersJson.Reserve(Entry.BBoxCornersWorldMeters.Num() * 2);
		for (const FVector& CornerMeters : Entry.BBoxCornersWorldMeters)
		{
			FVector2D CornerScreen;
			const bool bCornerInFront = ProjectWorldToScreenUnclamped(CornerMeters * 100.0, CornerScreen);
			CornersJson.Add(MakeShared<FJsonValueNumber>(bCornerInFront ? CornerScreen.X : -1.0));
			CornersJson.Add(MakeShared<FJsonValueNumber>(bCornerInFront ? CornerScreen.Y : -1.0));
		}

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
		ObjectJson->SetArrayField (TEXT("corners_px"), CornersJson);
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
