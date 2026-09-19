#include "CesiumSurroundingsScannerComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"

#include "CesiumMetadataPickingBlueprintLibrary.h"
#include "CesiumMetadataValue.h"
#include "Cesium3DTileset.h"

#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "CollisionShape.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UCesiumSurroundingsScannerComponent::UCesiumSurroundingsScannerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UCesiumSurroundingsScannerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		CameraComponent       = Owner->FindComponentByClass<UUAVCameraComponent>();
		SceneCaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	}

	Tileset = Cast<ACesium3DTileset>(UGameplayStatics::GetActorOfClass(GetWorld(), ACesium3DTileset::StaticClass()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Тік
// ─────────────────────────────────────────────────────────────────────────────

void UCesiumSurroundingsScannerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateSensorSize();
	Scan();
	BuildSensorFrame();
}

// ─────────────────────────────────────────────────────────────────────────────
// Розгортка сканування — сітка напрямків HorizontalRays x VerticalLayers, що охоплює точно
// HorizontalFOVDeg x VerticalFOVDeg камери, кожен напрямок розгортається як товста сфера
// (SweepMultiByChannel) замість трасування лінією нульової товщини, тому проміжки між
// сусідніми за кутом променями не дають об'єктам "проскочити" на дальності.
// ─────────────────────────────────────────────────────────────────────────────

TArray<FHitResult> UCesiumSurroundingsScannerComponent::SweepScan(const FTransform& OriginTransform, AActor* ActorToIgnore) const
{
	TArray<FHitResult> ScanResults;

	UWorld* World = GetWorld();
	if (!World) return ScanResults;

	const FVector Origin = OriginTransform.GetLocation();

	FCollisionQueryParams QueryParams;
	if (ActorToIgnore) QueryParams.AddIgnoredActor(ActorToIgnore);
	// bTraceComplex=true тут обов'язковий: FHitResult::FaceIndex (потрібен
	// GetPropertyTableValuesFromHit для визначення ID неінстансованого об'єкта)
	// заповнюється лише для влучань зі складною колізією — прості форми колізії
	// не мають поточкового зв'язку з візуальним мешем тайлсету й завжди повертають -1.
	QueryParams.bTraceComplex    = true;
	QueryParams.bReturnFaceIndex = true;

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(SweepRadius);
	const float Range = ScanRadiusMeters * 100.0f;

	const float HalfHFovRad = FMath::DegreesToRadians(CameraComponent->HorizontalFOVDeg * 0.5f);
	const float HalfVFovRad = FMath::DegreesToRadians(CameraComponent->VerticalFOVDeg * 0.5f);

	// Після заданої кількості сканувань повністю припиняємо сканувати нижню половину
	// вертикального FOV (усе нижче поздовжньої осі камери) — ScanCount збільшується на одиницю
	// за кожен виклик Scan(), див. Scan().
	const bool bCutoffLowerHalf = FramesBeforeLowerHalfCutoff > 0 && ScanCount > FramesBeforeLowerHalfCutoff;

	// Відладочний каркас вимикається разом із компонентом — малюється лише поки bSensorEnabled.
	if (bDrawScanArea && bSensorEnabled)
	{
		DrawScanAreaDebug(OriginTransform, Range, HalfHFovRad, bCutoffLowerHalf ? 0.0f : -HalfVFovRad, HalfVFovRad);
	}

	// Широкофазний прохід: звужує сітку до клітинок, що потенційно можуть влучити у вже
	// завантажений тайл Cesium, перш ніж витрачати ресурси на хоча б один фізичний запит.
	// Unset (а не просто порожній набір) означає "тайлсет не знайдено" — тоді повертаємось до
	// сканування кожної клітинки, як і до цієї оптимізації.
	TOptional<TSet<int32>> ActiveCells;
	if (Tileset)
	{
		const TArray<UPrimitiveComponent*> Candidates = GatherNearbyTileComponents(Origin, Range);
		if (Candidates.Num() == 0)
			return ScanResults; // у межах дальності немає нічого пов'язаного з Cesium — сканувати нема сенсу

		ActiveCells = BuildActiveCellSet(OriginTransform, Candidates, HalfHFovRad, HalfVFovRad);
	}

	for (int32 V = 0; V < VerticalLayers; ++V)
	{
		float VAngleRad = 0.0f;
		if (VerticalLayers > 1)
			VAngleRad = -HalfVFovRad + V * (2.0f * HalfVFovRad / (VerticalLayers - 1));

		if (bCutoffLowerHalf && VAngleRad < 0.0f)
			continue;

		for (int32 H = 0; H < HorizontalRays; ++H)
		{
			if (ActiveCells.IsSet() && !ActiveCells->Contains(V * HorizontalRays + H))
				continue;

			float HAngleRad = 0.0f;
			if (HorizontalRays > 1)
				HAngleRad = -HalfHFovRad + H * (2.0f * HalfHFovRad / (HorizontalRays - 1));

			// Прямокутна (перспективно-проєкційна) сітка напрямків: atan2(Y,X) == HAngleRad і
			// atan2(Z,X) == VAngleRad точно після нормалізації, тож кожен згенерований промінь
			// потрапляє точно всередину фрустуму камери — та сама незалежна перевірка
			// горизонтального/вертикального кута, яку використовує перспективна камера — і ніщо
			// поза ним ніколи не сканується.
			const FVector LocalDir(1.0f, FMath::Tan(HAngleRad), FMath::Tan(VAngleRad));
			const FVector WorldDir = OriginTransform.TransformVectorNoScale(LocalDir).GetSafeNormal();

			TArray<FHitResult> Hits;
			World->SweepMultiByChannel(Hits, Origin, Origin + WorldDir * Range, FQuat::Identity,
				CollisionChannel, SweepShape, QueryParams);

			for (FHitResult& Hit : Hits)
			{
				if (Hit.GetActor()) ScanResults.Add(Hit);
			}
		}
	}

	return ScanResults;
}

// ─────────────────────────────────────────────────────────────────────────────
// Каркас зони сканування — дешевий контур фрустуму з 8 ліній (а не по лінії на кожну клітинку
// сітки), що показує точно те, що зараз охоплює SweepScan, перемальовується заново щоскану
// (LifeTime -1.f, та сама угода про оновлення в один кадр, що й у променях об'єктів у Scan()).
// ─────────────────────────────────────────────────────────────────────────────

void UCesiumSurroundingsScannerComponent::DrawScanAreaDebug(const FTransform& OriginTransform, float Range,
	float HalfHFovRad, float VMinRad, float VMaxRad) const
{
	UWorld* World = GetWorld();
	if (!World) return;

	auto DirAt = [&OriginTransform](float HAngleRad, float VAngleRad) -> FVector
	{
		const FVector LocalDir(1.0f, FMath::Tan(HAngleRad), FMath::Tan(VAngleRad));
		return OriginTransform.TransformVectorNoScale(LocalDir).GetSafeNormal();
	};

	const FVector Origin = OriginTransform.GetLocation();
	const FVector TopLeft     = Origin + DirAt(-HalfHFovRad, VMaxRad) * Range;
	const FVector TopRight    = Origin + DirAt(+HalfHFovRad, VMaxRad) * Range;
	const FVector BottomLeft  = Origin + DirAt(-HalfHFovRad, VMinRad) * Range;
	const FVector BottomRight = Origin + DirAt(+HalfHFovRad, VMinRad) * Range;

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
// Широкофазний прохід — дешеві перевірки меж по вже завантажених компонентах тайлів Cesium,
// без залучення фізики, використовується, щоб пропускати клітинки сітки SweepScan, які нічого
// не можуть влучити.
// ─────────────────────────────────────────────────────────────────────────────

TArray<UPrimitiveComponent*> UCesiumSurroundingsScannerComponent::GatherNearbyTileComponents(const FVector& Origin, float RangeCm) const
{
	TArray<UPrimitiveComponent*> Candidates;
	if (!Tileset) return Candidates;

	TArray<UPrimitiveComponent*> AllPrimitives;
	Tileset->GetComponents<UPrimitiveComponent>(AllPrimitives);

	for (UPrimitiveComponent* Primitive : AllPrimitives)
	{
		if (!Primitive || !Primitive->IsRegistered()) continue;

		const FBoxSphereBounds& Bounds = Primitive->Bounds;
		const float DistanceToSurface = FVector::Dist(Bounds.Origin, Origin) - Bounds.SphereRadius;
		if (DistanceToSurface <= RangeCm)
			Candidates.Add(Primitive);
	}

	return Candidates;
}

TSet<int32> UCesiumSurroundingsScannerComponent::BuildActiveCellSet(const FTransform& OriginTransform,
	const TArray<UPrimitiveComponent*>& Candidates, float HalfHFovRad, float HalfVFovRad) const
{
	TSet<int32> ActiveCells;

	const float HStep = (HorizontalRays > 1) ? (2.0f * HalfHFovRad / (HorizontalRays - 1)) : 0.0f;
	const float VStep = (VerticalLayers > 1) ? (2.0f * HalfVFovRad / (VerticalLayers - 1)) : 0.0f;

	auto ActivateFullGrid = [this, &ActiveCells]()
	{
		for (int32 V = 0; V < VerticalLayers; ++V)
			for (int32 H = 0; H < HorizontalRays; ++H)
				ActiveCells.Add(V * HorizontalRays + H);
	};

	for (UPrimitiveComponent* Primitive : Candidates)
	{
		const FBoxSphereBounds& Bounds = Primitive->Bounds;
		const FVector LocalOrigin = OriginTransform.InverseTransformPosition(Bounds.Origin);

		// Охоплює або лежить позаду площини камери — кути на основі atan2 тут ненадійні, тож
		// обережно скануємо всю сітку цього скану, щоб не ризикувати мовчазно втратити об'єкт.
		if (LocalOrigin.X <= KINDA_SMALL_NUMBER)
		{
			ActivateFullGrid();
			continue;
		}

		const float AngularRadius = FMath::Atan2(Bounds.SphereRadius, LocalOrigin.Size());
		const float HAngle = FMath::Atan2(LocalOrigin.Y, LocalOrigin.X);
		const float VAngle = FMath::Atan2(LocalOrigin.Z, LocalOrigin.X);

		const float RawHMin = HAngle - AngularRadius;
		const float RawHMax = HAngle + AngularRadius;
		const float RawVMin = VAngle - AngularRadius;
		const float RawVMax = VAngle + AngularRadius;

		if (RawHMax < -HalfHFovRad || RawHMin > HalfHFovRad || RawVMax < -HalfVFovRad || RawVMin > HalfVFovRad)
			continue; // кутовий відбиток повністю поза FOV камери

		const float HMin = FMath::Clamp(RawHMin, -HalfHFovRad, HalfHFovRad);
		const float HMax = FMath::Clamp(RawHMax, -HalfHFovRad, HalfHFovRad);
		const float VMin = FMath::Clamp(RawVMin, -HalfVFovRad, HalfVFovRad);
		const float VMax = FMath::Clamp(RawVMax, -HalfVFovRad, HalfVFovRad);

		const int32 HIndexMin = (HStep > 0.0f) ? FMath::FloorToInt((HMin + HalfHFovRad) / HStep) : 0;
		const int32 HIndexMax = (HStep > 0.0f) ? FMath::CeilToInt((HMax + HalfHFovRad) / HStep) : HorizontalRays - 1;
		const int32 VIndexMin = (VStep > 0.0f) ? FMath::FloorToInt((VMin + HalfVFovRad) / VStep) : 0;
		const int32 VIndexMax = (VStep > 0.0f) ? FMath::CeilToInt((VMax + HalfVFovRad) / VStep) : VerticalLayers - 1;

		for (int32 V = FMath::Clamp(VIndexMin, 0, VerticalLayers - 1); V <= FMath::Clamp(VIndexMax, 0, VerticalLayers - 1); ++V)
			for (int32 H = FMath::Clamp(HIndexMin, 0, HorizontalRays - 1); H <= FMath::Clamp(HIndexMax, 0, HorizontalRays - 1); ++H)
				ActiveCells.Add(V * HorizontalRays + H);
	}

	return ActiveCells;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ідентичність об'єкта — той самий актор/компонент + однакові метадані означають "той самий
// об'єкт", використовується як для об'єднання дублікатів влучань у межах скану, так і як
// ключ ObjectStorage.
// ─────────────────────────────────────────────────────────────────────────────

FString UCesiumSurroundingsScannerComponent::BuildFeatureKey(const FCesiumSurroundingObject& Entry)
{
	FString Key = Entry.ActorName + TEXT("|") + Entry.ComponentName;

	TArray<FString> MetadataKeys;
	Entry.Metadata.GetKeys(MetadataKeys);
	MetadataKeys.Sort();
	for (const FString& MetaKey : MetadataKeys)
		Key += TEXT("|") + MetaKey + TEXT("=") + Entry.Metadata[MetaKey];

	return Key;
}

// ─────────────────────────────────────────────────────────────────────────────
// Об'єднання — згортає кілька влучань на той самий об'єкт (той самий актор/компонент і
// однакові метадані) в один запис, усереднюючи їхні позиції влучання.
// ─────────────────────────────────────────────────────────────────────────────

TArray<FCesiumSurroundingObject> UCesiumSurroundingsScannerComponent::MergeDuplicateHits(const TArray<FCesiumSurroundingObject>& RawEntries)
{
	struct FMergeAccumulator
	{
		FCesiumSurroundingObject Entry;
		FVector LocationSum = FVector::ZeroVector;
		int32   Count       = 0;
	};

	TMap<FString, FMergeAccumulator> Accumulators;

	for (const FCesiumSurroundingObject& Raw : RawEntries)
	{
		const FString Key = BuildFeatureKey(Raw);

		if (FMergeAccumulator* Existing = Accumulators.Find(Key))
		{
			Existing->LocationSum += Raw.HitLocationMeters;
			Existing->Count       += 1;
			Existing->Entry.DistanceMeters = FMath::Min(Existing->Entry.DistanceMeters, Raw.DistanceMeters);
		}
		else
		{
			FMergeAccumulator NewAccumulator;
			NewAccumulator.Entry       = Raw;
			NewAccumulator.LocationSum = Raw.HitLocationMeters;
			NewAccumulator.Count       = 1;
			Accumulators.Add(Key, MoveTemp(NewAccumulator));
		}
	}

	TArray<FCesiumSurroundingObject> Merged;
	Merged.Reserve(Accumulators.Num());
	for (TPair<FString, FMergeAccumulator>& Pair : Accumulators)
	{
		Pair.Value.Entry.HitLocationMeters = Pair.Value.LocationSum / Pair.Value.Count;
		Merged.Add(MoveTemp(Pair.Value.Entry));
	}

	return Merged;
}

// ─────────────────────────────────────────────────────────────────────────────
// ObjectStorage — єдині дві операції, що його змінюють.
// ─────────────────────────────────────────────────────────────────────────────

void UCesiumSurroundingsScannerComponent::AddObject(const FString& Key, const FCesiumSurroundingObject& Entry)
{
	FCesiumSurroundingObject StoredEntry = Entry;
	StoredEntry.ObjectID                 = Key;
	ObjectStorage.Add(Key, StoredEntry);

	FString MetadataLine;
	for (const TPair<FString, FString>& Pair : Entry.Metadata)
	{
		if (!MetadataLine.IsEmpty()) MetadataLine += TEXT(", ");
		MetadataLine += FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value);
	}

	const AActor* Owner = GetOwner();
}

void UCesiumSurroundingsScannerComponent::RemoveObject(const FString& Key)
{
	ObjectStorage.Remove(Key);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan — виконує SweepScan, зчитує таблицю властивостей Cesium кожного враженого об'єкта
// через GetPropertyTableValuesFromHit, об'єднує дублікати влучань на той самий об'єкт, потім
// звіряє результат з ObjectStorage: щойно побачені об'єкти додаються, об'єкти, у які й далі
// влучають (або пропущені, але все ще проєктуються в кадр камери), залишаються незмінними, а
// видаляються лише ті, чия заморожена позиція справді покинула кадр. LatestScanResults і
// відладочні промені керуються з ObjectStorage.
// ─────────────────────────────────────────────────────────────────────────────

const TArray<FCesiumSurroundingObject>& UCesiumSurroundingsScannerComponent::Scan()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || !CameraComponent || !SceneCaptureComponent)
	{
		LatestScanResults.Reset();
		return LatestScanResults;
	}

	++ScanCount;
	const TArray<FHitResult> Hits = SweepScan(SceneCaptureComponent->GetComponentTransform(), Owner);

	TArray<FCesiumSurroundingObject> RawEntries;
	for (const FHitResult& Hit : Hits)
	{
		TMap<FString, FCesiumMetadataValue> Values =
			UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(Hit, FeatureIDSetIndex);
		if (Values.Num() == 0) continue; // не Cesium-об'єкт із таблицею властивостей

		FCesiumSurroundingObject Entry;
		Entry.ActorName         = Hit.GetActor()->GetName();
		Entry.ComponentName     = Hit.GetComponent() ? Hit.GetComponent()->GetName() : FString();
		Entry.DistanceMeters    = Hit.Distance * 0.01f;
		Entry.HitLocationMeters = Hit.ImpactPoint * 0.01;

		for (const TPair<FString, FCesiumMetadataValue>& Pair : Values)
		{
			// Сигнальні значення "немає даних" декодуються в порожній FCesiumMetadataValue —
			// пропускаємо їх, а також усе, що все одно перетворюється на рядок "" (наприклад,
			// властивості типу масив).
			if (UCesiumMetadataValueBlueprintLibrary::IsEmpty(Pair.Value)) continue;

			FString ValueString = UCesiumMetadataValueBlueprintLibrary::GetString(Pair.Value, FString());
			if (ValueString.IsEmpty()) continue;

			Entry.Metadata.Add(Pair.Key, ValueString);
		}
		if (Entry.Metadata.Num() == 0) continue; // жодної непорожньої властивості — не логуємо

		RawEntries.Add(MoveTemp(Entry));
	}

	const TArray<FCesiumSurroundingObject> CurrentlyVisible = MergeDuplicateHits(RawEntries);

	// Звірка: додаємо все, що щойно стало видимим (пропускаючи те, що вже збережено — його
	// дані залишаються замороженими), потім видаляємо все, що випало з поля зору.
	TSet<FString> CurrentKeys;
	CurrentKeys.Reserve(CurrentlyVisible.Num());
	for (const FCesiumSurroundingObject& Entry : CurrentlyVisible)
	{
		FString Key = BuildFeatureKey(Entry);
		CurrentKeys.Add(Key);
		if (!ObjectStorage.Contains(Key))
			AddObject(Key, Entry);
	}

	// Об'єкт, у який розгортка цього скану не влучила, не обов'язково зник — миттєвий проміжок
	// між променями чи коротка оклюзія не повинні змушувати його блимати. Перепроєктовуємо
	// його заморожену позицію на камеру й видаляємо лише тоді, коли ця позиція справді покинула
	// кадр (або опинилась позаду камери); інакше продовжуємо відтворювати його з останньої
	// відомої позиції.
	TArray<FString> KeysNoLongerVisible;
	for (const TPair<FString, FCesiumSurroundingObject>& Pair : ObjectStorage)
	{
		if (CurrentKeys.Contains(Pair.Key))
			continue;

		FVector2D ScreenPos;
		const bool bStillInFrame = ProjectWorldToScreen(Pair.Value.HitLocationMeters * 100.0, ScreenPos);
		if (!bStillInFrame)
			KeysNoLongerVisible.Add(Pair.Key);
	}
	for (const FString& Key : KeysNoLongerVisible)
		RemoveObject(Key);

	// LatestScanResults відображає заморожені дані ObjectStorage, а не свіжі влучання цього скану.
	LatestScanResults.Reset();
	LatestScanResults.Reserve(ObjectStorage.Num());
	for (const TPair<FString, FCesiumSurroundingObject>& Pair : ObjectStorage)
		LatestScanResults.Add(Pair.Value);

	// Відладочні промені вимикаються разом із компонентом — малюються лише поки bSensorEnabled.
	if (bSensorEnabled)
	{
		for (const TPair<FString, FCesiumSurroundingObject>& Pair : ObjectStorage)
		{
			// По одному променю на кожен відстежуваний об'єкт, перемальовується заново щотіку
			// (LifeTime -1.f, та сама угода про оновлення в один кадр, що й у стрілках сил
			// USubAerodynamicSurfaceSC), тому він ніколи не накопичується у "віяло" минулих
			// позицій — завжди вказує від поточної позиції літака до об'єкта.
			if (bDrawRayDebug)
				DrawDebugLine(World, Owner->GetActorLocation(), Pair.Value.HitLocationMeters * 100.0, RayDebugColor, false, -1.0f);
		}
	}

	return LatestScanResults;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface
// ─────────────────────────────────────────────────────────────────────────────

bool UCesiumSurroundingsScannerComponent::GetLatestFrame(FSensorFrame& OutFrame)
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
// статичний об'єкт світу). Перелік полів — див. документацію заголовка BuildSensorFrame().
// ─────────────────────────────────────────────────────────────────────────────

void UCesiumSurroundingsScannerComponent::UpdateSensorSize()
{
	// UAVCameraComponent призначає TextureTarget у власному BeginPlay; перечитуємо, доки не буде дійсним.
	if (SceneCaptureComponent && (SensorSizeX <= 0 || SensorSizeY <= 0) && SceneCaptureComponent->TextureTarget)
	{
		SensorSizeX = SceneCaptureComponent->TextureTarget->SizeX;
		SensorSizeY = SceneCaptureComponent->TextureTarget->SizeY;
	}
}

void UCesiumSurroundingsScannerComponent::BuildSensorFrame()
{
	if (!bSensorEnabled || !SceneCaptureComponent)
	{
		bHasFrame = false;
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ObjectsJson;
	ObjectsJson.Reserve(ObjectStorage.Num());

	for (const TPair<FString, FCesiumSurroundingObject>& Pair : ObjectStorage)
	{
		const FCesiumSurroundingObject& Entry = Pair.Value;

		FString ObjID;
		double Latitude  = 0.0;
		double Longitude = 0.0;
		double Altitude  = 0.0;
		if (const FString* Found = Entry.Metadata.Find(ObjectPropertyName))  ObjID = *Found;
		if (const FString* Found = Entry.Metadata.Find(LatitudePropertyName))  Latitude  = FCString::Atod(**Found);
		if (const FString* Found = Entry.Metadata.Find(LongitudePropertyName)) Longitude = FCString::Atod(**Found);
		if (const FString* Found = Entry.Metadata.Find(AltitudePropertyName))  Altitude  = FCString::Atod(**Found);

		FVector2D ScreenPos;
		const bool bVisible = ProjectWorldToScreen(Entry.HitLocationMeters * 100.0, ScreenPos);

		TSharedRef<FJsonObject> ObjectJson = MakeShared<FJsonObject>();
		ObjectJson->SetStringField(TEXT("id"),	      ObjID);
		ObjectJson->SetNumberField(TEXT("latitude"),  Latitude);
		ObjectJson->SetNumberField(TEXT("longitude"), Longitude);
		ObjectJson->SetNumberField(TEXT("altitude"),  Altitude);
		ObjectJson->SetNumberField(TEXT("pixel_x"),   bVisible ? ScreenPos.X : -1.0);
		ObjectJson->SetNumberField(TEXT("pixel_y"),   bVisible ? ScreenPos.Y : -1.0);
		ObjectJson->SetBoolField  (TEXT("visible"),   bVisible);
		ObjectsJson.Add(MakeShared<FJsonValueObject>(ObjectJson));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("objects"), ObjectsJson);

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
// UKeyPointDetectionComponent::ProjectWorldToScreen.
// ─────────────────────────────────────────────────────────────────────────────

bool UCesiumSurroundingsScannerComponent::ProjectWorldToScreen(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const
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

	return ScreenX >= 0.0f && ScreenX <= static_cast<float>(SensorSizeX) &&
	       ScreenY >= 0.0f && ScreenY <= static_cast<float>(SensorSizeY);
}
