#include "CustomSurroundingsScannerComponent.h"
#include "UAVSimulator/UAVSimulator.h"

#include "CesiumGeoreference.h"
#include "Cesium3DTileset.h"

#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

// Заглушка для майбутнього джерела на основі файлу/мережі — наразі захардкоджено тут, за схемою з
// Tools/TestingPlatform/attitude_control/map_objects.json: кожен запис —
// {"elementId": "...", "type": "...", "altitude": ..., "bbox": {x_min, x_max, y_min, y_max}}, де
// кожен кут — це пара {"latitude": ..., "longitude": ...}. Запис без придатного "bbox"
// пропускається.
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

	TArray<AActor*> Tilesets;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACesium3DTileset::StaticClass(), Tilesets);
	Tileset = Tilesets.Num() > 0 ? Cast<ACesium3DTileset>(Tilesets[0]) : nullptr;
	UE_LOG(LogUAV, Log, TEXT("CustomSurroundingsScanner: знайдено %d Cesium3DTileset, Georeference=%s, канал трейсу=%d"),
		Tilesets.Num(), Georeference ? TEXT("OK") : TEXT("null"), (int32)GroundTraceChannel.GetValue());

	LoadObjects();
	LastLoadedObjectsJson = ObjectsJson;
}

// ─────────────────────────────────────────────────────────────────────────────
// Тік
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Живе перезавантаження: підхоплює редагування ObjectsJson (кути bbox, тип, висота тощо) без
	// потреби перезапуску. Scan() нижче ресинхронізує вже видимі записи ObjectStorage зі свіжо
	// перезавантаженого AllObjects.
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
// Load — (пере)парсовує ObjectsJson, перетворюючи чотири кути "bbox" кожного запису у світові
// координати через Georeference (BBoxCornersWorldMeters) і беручи їхнє середнє як центр
// відбитка (Latitude/Longitude/WorldLocationMeters). Кути перетворюються на висоті еліпсоїда 0 —
// "altitude" з JSON ігнорується; далі Scan() прив'язує висоту кожного кута до поверхні тайла
// Cesium (ResolveGroundHeights). Викликається з BeginPlay і повторно з TickComponent щоразу, коли
// змінюється ObjectsJson (див. LastLoadedObjectsJson); DistanceMeters додатково оновлюється кожен
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

	// Чотири назви кутів "bbox", у порядку обходу — чотирикутник малюється / проєктується саме в цьому порядку.
	static const TCHAR* CornerNames[] = { TEXT("x_min"), TEXT("x_max"), TEXT("y_min"), TEXT("y_max") };

	for (const TSharedPtr<FJsonValue>& Value : ParsedArray)
	{
		const TSharedPtr<FJsonObject>* JsonObject;
		if (!Value->TryGetObject(JsonObject)) continue;

		FCustomSurroundingObject Entry;
		(*JsonObject)->TryGetStringField(TEXT("elementId"), Entry.ObjectID);
		(*JsonObject)->TryGetStringField(TEXT("type"), Entry.ObjectType);
		// "altitude" все ще зчитується (вона повертається назад у навантаженні сенсора), але НЕ
		// використовується для розміщення маркерів — натомість Scan() прив'язує їхню висоту до
		// поверхні тайла Cesium.
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

			// Перетворення на висоті еліпсоїда 0 — "altitude" з JSON ігнорується. Scan() замінює
			// висоту трасою на поверхню тайла Cesium (див. ResolveGroundHeights).
			Entry.BBoxCornersWorldMeters.Add(GeoToWorldMeters(CornerLat, CornerLong, 0.0));

			SumLat  += CornerLat;
			SumLong += CornerLong;
			++CornerCount;
		}

		if (CornerCount == 0)
		{
			UE_LOG(LogUAV, Warning, TEXT("CustomSurroundingsScanner: у об'єкта %s порожній \"bbox\" — пропущено"), *Entry.ObjectID);
			continue;
		}

		// Центр відбитка — середнє розпарсованих кутів, як у географічних, так і у світових координатах.
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
// Допоміжні функції geo → world + прив'язка до землі. "altitude" з JSON ніколи не
// використовується для розміщення маркера; натомість кожен кут трасується прямо вниз/вгору на
// поверхню тайла Cesium, тож маркери завжди лежать точно на висоті рельєфу.
// ResolveGroundHeights повторюється кожен Scan(), доки не влучить, оскільки Cesium стрімить
// тайли за дистанцією до камери.
// ─────────────────────────────────────────────────────────────────────────────

FVector UCustomSurroundingsScannerComponent::GeoToWorldMeters(double LatitudeDeg, double LongitudeDeg, double HeightMeters) const
{
	if (!Georeference) return FVector::ZeroVector;

	const FVector LongitudeLatitudeHeight(LongitudeDeg, LatitudeDeg, HeightMeters);
	const FVector UnrealPositionCm = Georeference->GetActorTransform().TransformPosition(
		Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(LongitudeLatitudeHeight));
	return UnrealPositionCm * 0.01;
}

FVector UCustomSurroundingsScannerComponent::GeographicUpMeters(double LatitudeDeg, double LongitudeDeg) const
{
	if (!Georeference) return FVector::UpVector;

	const FVector Low  = GeoToWorldMeters(LatitudeDeg, LongitudeDeg, 0.0);
	const FVector High = GeoToWorldMeters(LatitudeDeg, LongitudeDeg, 1000.0);
	const FVector Up   = (High - Low).GetSafeNormal();
	return Up.IsNearlyZero() ? FVector::UpVector : Up;
}

bool UCustomSurroundingsScannerComponent::TryTraceTileSurfaceMeters(const FVector& FromPointMeters, const FVector& UpMeters, FVector& OutSurfacePointMeters) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	const FVector UpDir  = UpMeters.GetSafeNormal();
	if (UpDir.IsNearlyZero()) return false;

	const FVector FromCm = FromPointMeters * 100.0;
	const double  SpanCm = FMath::Max(GroundTraceSpanMeters, 1.0f) * 100.0;
	const FVector Start  = FromCm + UpDir * SpanCm;
	const FVector End    = FromCm - UpDir * SpanCm;

	FCollisionQueryParams Params(TEXT("CustomSurroundingsGroundTrace"), /*bTraceComplex=*/true, GetOwner());

	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, End, GroundTraceChannel, Params);

	const FHitResult* Accepted = nullptr;
	for (const FHitResult& Hit : Hits)
	{
		// Приймаємо влучання в будь-який Cesium3DTileset (сцена може мати декілька — рельєф,
		// будівлі тощо), а не лише перший, знайдений у BeginPlay. bRequireCesiumTilesetHit можна
		// вимкнути, щоб натомість прив'язуватися до будь-якої блокуючої геометрії на цьому каналі.
		const AActor* HitActor = Hit.GetActor();
		if (bRequireCesiumTilesetHit && (!HitActor || !HitActor->IsA<ACesium3DTileset>()))
			continue;

		Accepted = &Hit;
		break;
	}

	if (bDebugGroundTrace)
	{
		// LifeTime -1.f → перемальовується заново щотіку, та сама угода, що й для інших відладочних маркерів у цьому файлі.
		const FColor LineColor = Accepted ? FColor::Green : FColor::Red;
		DrawDebugLine(World, Start, End, LineColor, false, -1.0f, 0, 20.0f);
		if (Accepted)
			DrawDebugSphere(World, Accepted->ImpactPoint, 300.0f, 12, FColor::Green, false, -1.0f);

		// Обмежено за частотою, щоб постійно промахувана траса не заповнювала лог щотіку.
		const double Now = World->GetTimeSeconds();
		if (!Accepted && Now - LastGroundTraceLogTime > 2.0)
		{
			LastGroundTraceLogTime = Now;
			if (Hits.Num() > 0)
			{
				const AActor* FirstActor = Hits[0].GetActor();
				UE_LOG(LogUAV, Warning, TEXT("CustomSurroundingsScanner: трейс влучив у %s (%s), але bRequireCesiumTilesetHit його відкинув"),
					FirstActor ? *FirstActor->GetName() : TEXT("null"),
					FirstActor ? *FirstActor->GetClass()->GetName() : TEXT("null"));
			}
			else
			{
				UE_LOG(LogUAV, Warning, TEXT("CustomSurroundingsScanner: вертикальний трейс не влучив ні в що (канал=%d, довжина=%.0f м) — перевір колізію на Cesium3DTileset"),
					(int32)GroundTraceChannel.GetValue(), GroundTraceSpanMeters * 2.0f);
			}
		}
	}

	if (!Accepted)
		return false;

	OutSurfacePointMeters = Accepted->ImpactPoint * 0.01 + UpDir * GroundHeightOffsetMeters;
	return true;
}

void UCustomSurroundingsScannerComponent::ResolveGroundHeights(FCustomSurroundingObject& Object) const
{
	const int32 CornerCount = Object.BBoxCornersWorldMeters.Num();
	if (CornerCount == 0) return;

	// "вгору" майже не змінюється в межах одного відбитка — достатньо однієї осі, взятої в центрі.
	const FVector UpMeters = GeographicUpMeters(Object.Latitude, Object.Longitude);

	int32 HitCount = 0;
	for (FVector& Corner : Object.BBoxCornersWorldMeters)
	{
		FVector SurfacePoint;
		if (TryTraceTileSurfaceMeters(Corner, UpMeters, SurfacePoint))
		{
			Corner = SurfacePoint;   // стабільно при повторному запуску: наступна траса стартує з прив'язаної точки
			++HitCount;
		}
	}

	// Центр відбитка — середнє (тепер уже прив'язаних) кутів, та сама угода, що й у LoadObjects().
	FVector CentreSum = FVector::ZeroVector;
	for (const FVector& Corner : Object.BBoxCornersWorldMeters)
		CentreSum += Corner;
	Object.WorldLocationMeters = CentreSum / CornerCount;

	const bool bAllResolved = (HitCount == CornerCount);
	Object.bGroundHeightResolved = bAllResolved;

	// Спрацьовує один раз на об'єкт (щойно розв'язано, Scan() перестає це викликати). Випадок
	// промаху натомість повідомляється — з обмеженням частоти — з TryTraceTileSurfaceMeters, щоб
	// не заповнювати лог щотіку.
	if (bDebugGroundTrace && bAllResolved)
		UE_LOG(LogUAV, Log, TEXT("CustomSurroundingsScanner: %s приземлено на тайли (усі %d кути), центр Z=%.1f м"),
			*Object.ObjectID, CornerCount, Object.WorldLocationMeters.Z);
}

// ─────────────────────────────────────────────────────────────────────────────
// ObjectStorage — єдині дві операції, що його змінюють.
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
// Scan — заново перевіряє кожен завантажений об'єкт відносно поточної дальності/кадру камери,
// звіряє результат з ObjectStorage (додає щойно видимі, видаляє більше не видимі), потім
// перебудовує LatestScanResults і відладочні промені з ObjectStorage.
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
		// "altitude" із JSON ігнорується для розміщення — прив'язуємо кожен маркер до поверхні
		// тайла Cesium. Повторюється, доки кожен кут не отримає влучання, оскільки тайли
		// стрімляться за дистанцією до камери, і тайли віддаленого об'єкта можуть ще не існувати.
		if (bSnapMarkersToTileSurface && !Object.bGroundHeightResolved)
			ResolveGroundHeights(Object);

		Object.DistanceMeters = FVector::Dist(OwnerLocationMeters, Object.WorldLocationMeters);

		FVector2D ScreenPos;
		const bool bInRange = Object.DistanceMeters <= ScanRadiusMeters;
		const bool bVisible = bInRange && ProjectWorldToScreen(Object.WorldLocationMeters * 100.0, ScreenPos);
		if (!bVisible) continue;

		CurrentlyVisibleKeys.Add(Object.ObjectID);
		// Повне перезаписування (не лише DistanceMeters), щоб живе редагування ObjectsJson — кути
		// bbox, тип, висота — доходило до вже видимого об'єкта негайно, а не лише до щойно доданих.
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

	// Відладочні маркери вимикаються разом із компонентом — малюються лише поки bSensorEnabled.
	if (bSensorEnabled)
	{
		if (bDrawScanArea && SensorSizeX > 0 && SensorSizeY > 0)
		{
			const float AspectRatio = static_cast<float>(SensorSizeX) / static_cast<float>(SensorSizeY);
			const float HalfHFovRad = FMath::DegreesToRadians(SceneCaptureComponent->FOVAngle * 0.5f);
			const float HalfVFovRad = FMath::Atan(FMath::Tan(HalfHFovRad) / AspectRatio);
			DrawScanAreaDebug(SceneCaptureComponent->GetComponentTransform(), ScanRadiusMeters * 100.0f, HalfHFovRad, HalfVFovRad);
		}

		// По одному променю (+ опційно bbox) на кожен видимий об'єкт, перемальовується заново
		// щотіку (LifeTime -1.f, та сама угода про оновлення в один кадр, що й у променях
		// UCesiumSurroundingsScannerComponent).
		for (const TPair<FString, FCustomSurroundingObject>& Pair : ObjectStorage)
		{
			if (bDrawRayDebug)
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
// Каркас зони сканування — дешевий контур фрустуму з 8 ліній, що показує поточний вигляд камери
// до ScanRadiusMeters. Та сама форма/логіка, що й у
// UCesiumSurroundingsScannerComponent::DrawScanAreaDebug.
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

	// Чотири ребра від початку координат до дальніх кутів.
	DrawDebugLine(World, Origin, TopLeft,     ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, Origin, TopRight,    ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, Origin, BottomLeft,  ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, Origin, BottomRight, ScanAreaDebugColor, false, -1.0f);

	// Дальній прямокутник, що з'єднує чотири кути.
	DrawDebugLine(World, TopLeft,     TopRight,    ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, TopRight,    BottomRight, ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, BottomRight, BottomLeft,  ScanAreaDebugColor, false, -1.0f);
	DrawDebugLine(World, BottomLeft,  TopLeft,     ScanAreaDebugColor, false, -1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bbox об'єкта — чотирикутник відбитка об'єкта, його чотири кути "bbox" (BBoxCornersWorldMeters,
// порядок обходу x_min -> x_max -> y_min -> y_max), з'єднані ребро до ребра. Це реальні
// світові точки, тож чотирикутник лежить на фактичному відбитку — без білбордингу, без
// фіксованих референсних осей.
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
// Bbox об'єкта — проєктований розмір на екрані. Проєктує ті самі чотири кути, що малює
// DrawObjectBBoxDebug, тож повідомлений піксельний розмір точно відповідає світовому чотирикутнику.
// ─────────────────────────────────────────────────────────────────────────────

FVector2D UCustomSurroundingsScannerComponent::ComputeBBoxScreenSize(const TArray<FVector>& CornersMeters) const
{
	FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D Max(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	bool bAnyCornerInFront = false;

	for (const FVector& CornerMeters : CornersMeters)
	{
		FVector2D ScreenPos;
		if (!ProjectWorldToScreenUnclamped(CornerMeters * 100.0, ScreenPos)) continue; // позаду камери — пропускаємо

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
// Мітка об'єкта — текст "elementId", намальований одразу над точкою об'єкта.
// ─────────────────────────────────────────────────────────────────────────────

void UCustomSurroundingsScannerComponent::DrawObjectLabelDebug(const FVector& WorldPositionCm, const FString& Label) const
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Duration 0 → малюється лише для поточного кадру, та сама угода про перемальовування щотіку,
	// що й у інших відладочних маркерів (які використовують LifeTime -1.0f для того самого ефекту
	// на лініях/точках).
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
// Корисне навантаження сенсора — один JSON-об'єкт на кожен наразі видимий об'єкт
// (ObjectStorage), перепроєктований на камеру щотіку, оскільки рухається саме камера (а не
// статичний об'єкт).
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

		// Розмір bbox у піксельному просторі — проєктовані осьовирівняні межі чотирьох кутів
		// чотирикутника відбитка (BBoxCornersWorldMeters), та сама проєкція, що й у pixel_x/pixel_y нижче.
		const FVector2D BBoxScreenSize = ComputeBBoxScreenSize(Entry.BBoxCornersWorldMeters);

		// Кожен кут відбитка, проєктований у кадр, без обмеження меж, як плоский масив
		// [x0,y0,x1,y1,...] у порядку обходу x_min -> x_max -> y_min -> y_max — дозволяє
		// споживачу побудувати або точний осьовирівняний, або орієнтований (повернутий) бокс, що
		// природно підходить для чотирикутника відбитка, побаченого під косим курсом. Кут позаду
		// камери записується як [-1, -1].
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
// Проєкція — ідентичне налаштування вигляду/проєкції до
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
