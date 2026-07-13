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
		FString Key = Raw.ActorName + TEXT("|") + Raw.ComponentName;

		TArray<FString> MetadataKeys;
		Raw.Metadata.GetKeys(MetadataKeys);
		MetadataKeys.Sort();
		for (const FString& MetaKey : MetadataKeys)
			Key += TEXT("|") + MetaKey + TEXT("=") + Raw.Metadata[MetaKey];

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
// Scan — runs SweepScan, then reads the Cesium property table of every hit
// feature via GetPropertyTableValuesFromHit, merges duplicate hits on the same
// feature, logs each merged feature, and draws a debug ray to it.
// ─────────────────────────────────────────────────────────────────────────────

const TArray<FCesiumSurroundingObject>& UCesiumSurroundingsScannerComponent::Scan()
{
	LatestScanResults.Reset();

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || !CameraComponent || !SceneCaptureComponent) return LatestScanResults;

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

	for (FCesiumSurroundingObject& Entry : MergeDuplicateHits(RawEntries))
	{
		FString MetadataLine;
		for (const TPair<FString, FString>& Pair : Entry.Metadata)
		{
			if (!MetadataLine.IsEmpty()) MetadataLine += TEXT(", ");
			MetadataLine += FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value);
		}

		UE_LOG(LogUAV, Log, TEXT("CesiumSurroundingsScanner: %s і (%s) — %.1f м від %s: %s"),
			*Entry.ActorName, *Entry.ComponentName, Entry.DistanceMeters, *Owner->GetName(), *MetadataLine);

		// Ray to the scanned feature — same visualization technique (line trace + "Draw
		// Debug Type: For Duration") the Cesium metadata-picking tutorial uses. Drawn from the
		// camera (the actual sweep origin), not the owner's actor origin.
		DrawDebugLine(World, SceneCaptureComponent->GetComponentLocation(), Entry.HitLocationMeters * 100.0, RayDebugColor, false, RayDebugDuration);

		LatestScanResults.Add(MoveTemp(Entry));
	}

	return LatestScanResults;
}
