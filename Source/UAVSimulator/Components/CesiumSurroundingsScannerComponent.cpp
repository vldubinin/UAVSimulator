#include "CesiumSurroundingsScannerComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"

#include "CesiumMetadataPickingBlueprintLibrary.h"
#include "CesiumMetadataValue.h"

#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "CollisionShape.h"

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
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UCesiumSurroundingsScannerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ScanAccumulator += DeltaTime;
	const float ScanInterval = 1.0f / FMath::Max(ScanRate, 0.01f);
	if (ScanAccumulator >= ScanInterval)
	{
		ScanAccumulator -= ScanInterval;
		Scan();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Sweep scan — a HorizontalRays x VerticalLayers grid of directions spanning exactly the
// camera's HorizontalFOVDeg x VerticalFOVDeg, each swept as a thick sphere
// (SweepMultiByChannel) instead of a zero-width line trace so gaps between angularly-
// adjacent rays don't let objects slip through at range.
// ─────────────────────────────────────────────────────────────────────────────

TArray<FHitResult> UCesiumSurroundingsScannerComponent::SweepScan(const FTransform& OriginTransform, AActor* ActorToIgnore) const
{
	TArray<FHitResult> ScanResults;

	UWorld* World = GetWorld();
	if (!World) return ScanResults;

	const FVector Origin = OriginTransform.GetLocation();

	FCollisionQueryParams QueryParams;
	if (ActorToIgnore) QueryParams.AddIgnoredActor(ActorToIgnore);
	// bTraceComplex=true is required here: FHitResult::FaceIndex (needed by
	// GetPropertyTableValuesFromHit to resolve a non-instanced feature's ID) is
	// only populated for complex-collision hits — simple collision shapes have
	// no per-triangle mapping to the tileset's visual mesh and always report -1.
	QueryParams.bTraceComplex    = true;
	QueryParams.bReturnFaceIndex = true;

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(SweepRadius);
	const float Range = ScanRadiusMeters * 100.0f;

	const float HalfHFovRad = FMath::DegreesToRadians(CameraComponent->HorizontalFOVDeg * 0.5f);
	const float HalfVFovRad = FMath::DegreesToRadians(CameraComponent->VerticalFOVDeg * 0.5f);

	for (int32 V = 0; V < VerticalLayers; ++V)
	{
		float VAngleRad = 0.0f;
		if (VerticalLayers > 1)
			VAngleRad = -HalfVFovRad + V * (2.0f * HalfVFovRad / (VerticalLayers - 1));

		for (int32 H = 0; H < HorizontalRays; ++H)
		{
			float HAngleRad = 0.0f;
			if (HorizontalRays > 1)
				HAngleRad = -HalfHFovRad + H * (2.0f * HalfHFovRad / (HorizontalRays - 1));

			// Rectangular (perspective-projection) direction grid: atan2(Y,X) == HAngleRad and
			// atan2(Z,X) == VAngleRad exactly after normalization, so every generated ray falls
			// precisely inside the camera's frustum — the same independent horizontal/vertical
			// angle test a perspective camera uses — with nothing outside it ever swept.
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
// Feature identity — same actor/component + identical metadata means "same feature",
// used both to merge duplicate hits within a scan and as the ObjectStorage key.
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
// Merge — collapses multiple hits on the same feature (same actor/component and
// identical metadata) into a single entry, averaging their hit locations.
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
// ObjectStorage — the only two operations that mutate it.
// ─────────────────────────────────────────────────────────────────────────────

void UCesiumSurroundingsScannerComponent::AddObject(const FString& Key, const FCesiumSurroundingObject& Entry)
{
	ObjectStorage.Add(Key, Entry);

	FString MetadataLine;
	for (const TPair<FString, FString>& Pair : Entry.Metadata)
	{
		if (!MetadataLine.IsEmpty()) MetadataLine += TEXT(", ");
		MetadataLine += FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value);
	}

	const AActor* Owner = GetOwner();
	UE_LOG(LogUAV, Log, TEXT("CesiumSurroundingsScanner: у полі зору з'явився %s (%s) — %.1f м від %s: %s"),
		*Entry.ActorName, *Entry.ComponentName, Entry.DistanceMeters, Owner ? *Owner->GetName() : TEXT("?"), *MetadataLine);
}

void UCesiumSurroundingsScannerComponent::RemoveObject(const FString& Key)
{
	ObjectStorage.Remove(Key);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan — runs SweepScan, reads the Cesium property table of every hit feature via
// GetPropertyTableValuesFromHit, merges duplicate hits on the same feature, then
// reconciles the result against ObjectStorage: newly-visible features are added,
// features no longer visible are removed, and features still visible are left
// untouched. LatestScanResults and the debug rays are driven from ObjectStorage.
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
			// "no data" sentinel values decode to an empty FCesiumMetadataValue — skip those,
			// and skip anything that still stringifies to "" (e.g. array-typed properties).
			if (UCesiumMetadataValueBlueprintLibrary::IsEmpty(Pair.Value)) continue;

			FString ValueString = UCesiumMetadataValueBlueprintLibrary::GetString(Pair.Value, FString());
			if (ValueString.IsEmpty()) continue;

			Entry.Metadata.Add(Pair.Key, ValueString);
		}
		if (Entry.Metadata.Num() == 0) continue; // жодної непорожньої властивості — не логуємо

		RawEntries.Add(MoveTemp(Entry));
	}

	const TArray<FCesiumSurroundingObject> CurrentlyVisible = MergeDuplicateHits(RawEntries);

	// Reconcile: add whatever just became visible (skip anything already stored — its data
	// stays frozen), then remove whatever dropped out of view.
	TSet<FString> CurrentKeys;
	CurrentKeys.Reserve(CurrentlyVisible.Num());
	for (const FCesiumSurroundingObject& Entry : CurrentlyVisible)
	{
		FString Key = BuildFeatureKey(Entry);
		CurrentKeys.Add(Key);
		if (!ObjectStorage.Contains(Key))
			AddObject(Key, Entry);
	}

	TArray<FString> KeysNoLongerVisible;
	for (const TPair<FString, FCesiumSurroundingObject>& Pair : ObjectStorage)
	{
		if (!CurrentKeys.Contains(Pair.Key))
			KeysNoLongerVisible.Add(Pair.Key);
	}
	for (const FString& Key : KeysNoLongerVisible)
		RemoveObject(Key);

	// Rays and LatestScanResults reflect ObjectStorage's frozen data, not this scan's fresh hits.
	LatestScanResults.Reset();
	LatestScanResults.Reserve(ObjectStorage.Num());
	for (const TPair<FString, FCesiumSurroundingObject>& Pair : ObjectStorage)
	{
		const FCesiumSurroundingObject& Entry = Pair.Value;

		// Ray to the scanned feature — same visualization technique (line trace + "Draw
		// Debug Type: For Duration") the Cesium metadata-picking tutorial uses. Drawn from the
		// camera (the actual sweep origin), not the owner's actor origin.
		DrawDebugLine(World, SceneCaptureComponent->GetComponentLocation(), Entry.HitLocationMeters * 100.0, RayDebugColor, false, RayDebugDuration);

		LatestScanResults.Add(Entry);
	}

	return LatestScanResults;
}
