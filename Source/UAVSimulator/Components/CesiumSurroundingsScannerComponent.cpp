#include "CesiumSurroundingsScannerComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/SensorUtilityLibrary.h"

#include "CesiumMetadataPickingBlueprintLibrary.h"
#include "CesiumMetadataValue.h"

#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UCesiumSurroundingsScannerComponent::UCesiumSurroundingsScannerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
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
// Scan — spherical ray sweep (USensorUtilityLibrary::FindActors), then reads the
// Cesium property table of every hit feature via GetPropertyTableValuesFromHit.
// ─────────────────────────────────────────────────────────────────────────────

const TArray<FCesiumSurroundingObject>& UCesiumSurroundingsScannerComponent::Scan()
{
	LatestScanResults.Reset();

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner) return LatestScanResults;

	LatestScanTimestamp = World->GetTimeSeconds();

	// bTraceComplex=true is required here: FHitResult::FaceIndex (needed by
	// GetPropertyTableValuesFromHit to resolve a non-instanced feature's ID) is
	// only populated for complex-collision hits — simple collision shapes have
	// no per-triangle mapping to the tileset's visual mesh and always report -1.
	const TArray<FHitResult> Hits = USensorUtilityLibrary::FindActors(
		this,
		Owner->GetActorTransform(),
		Owner,
		ScanRadiusMeters * 100.0f,
		HorizontalRays,
		VerticalLayers,
		VerticalFOVDeg,
		CollisionChannel,
		true);

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
		Entry.Metadata          = UCesiumMetadataValueBlueprintLibrary::GetValuesAsStrings(Values);

		FString MetadataLine;
		for (const TPair<FString, FString>& Pair : Entry.Metadata)
		{
			if (!MetadataLine.IsEmpty()) MetadataLine += TEXT(", ");
			MetadataLine += FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value);
		}

		UE_LOG(LogUAV, Log, TEXT("CesiumSurroundingsScanner: %s (%s) — %.1f м від %s: %s"),
			*Entry.ActorName, *Entry.ComponentName, Entry.DistanceMeters, *Owner->GetName(), *MetadataLine);

		LatestScanResults.Add(MoveTemp(Entry));
	}

	return LatestScanResults;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — called on game thread by SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool UCesiumSurroundingsScannerComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (LatestScanResults.IsEmpty()) return false;

	FString JsonString = SerializeResults(LatestScanResults);
	FTCHARToUTF8 Utf8(*JsonString);

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestScanTimestamp;
	OutFrame.Payload.Reset();
	OutFrame.Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialization
// ─────────────────────────────────────────────────────────────────────────────

FString UCesiumSurroundingsScannerComponent::SerializeResults(const TArray<FCesiumSurroundingObject>& Results) const
{
	TArray<TSharedPtr<FJsonValue>> JsonArray;

	for (const FCesiumSurroundingObject& Obj : Results)
	{
		TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
		JsonObj->SetStringField(TEXT("actor"), Obj.ActorName);
		JsonObj->SetStringField(TEXT("component"), Obj.ComponentName);
		JsonObj->SetNumberField(TEXT("distance_m"), Obj.DistanceMeters);

		TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
		Location->SetNumberField(TEXT("x_m"), Obj.HitLocationMeters.X);
		Location->SetNumberField(TEXT("y_m"), Obj.HitLocationMeters.Y);
		Location->SetNumberField(TEXT("z_m"), Obj.HitLocationMeters.Z);
		JsonObj->SetObjectField(TEXT("hit_location"), Location);

		TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Pair : Obj.Metadata)
			Metadata->SetStringField(Pair.Key, Pair.Value);
		JsonObj->SetObjectField(TEXT("metadata"), Metadata);

		JsonArray.Add(MakeShared<FJsonValueObject>(JsonObj));
	}

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(JsonArray, Writer);
	return Out;
}
