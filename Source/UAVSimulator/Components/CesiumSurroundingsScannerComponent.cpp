#include "CesiumSurroundingsScannerComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/SensorUtilityLibrary.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"

#include "CesiumMetadataPickingBlueprintLibrary.h"
#include "CesiumMetadataValue.h"

#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
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
		CameraComponent        = Owner->FindComponentByClass<UUAVCameraComponent>();
		SceneCaptureComponent  = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	}
}

void UCesiumSurroundingsScannerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearMarkers();
	Super::EndPlay(EndPlayReason);
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

	LogBuildingsInCameraFrame();
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame camera-frustum filter over the (periodically refreshed) scan cache.
// ─────────────────────────────────────────────────────────────────────────────

void UCesiumSurroundingsScannerComponent::LogBuildingsInCameraFrame() const
{
	if (!CameraComponent || !SceneCaptureComponent) return;

	const FTransform CameraTransform = SceneCaptureComponent->GetComponentTransform();
	const FVector     CameraLocation = CameraTransform.GetLocation();
	const float       HalfHFovRad    = FMath::DegreesToRadians(CameraComponent->HorizontalFOVDeg * 0.5f);
	const float       HalfVFovRad    = FMath::DegreesToRadians(CameraComponent->VerticalFOVDeg * 0.5f);

	for (const FCesiumSurroundingObject& Obj : LatestScanResults)
	{
		const FVector ToObject = (Obj.HitLocationMeters * 100.0) - CameraLocation;
		if (ToObject.IsNearlyZero()) continue;

		const FVector LocalDir = CameraTransform.InverseTransformVectorNoScale(ToObject.GetSafeNormal());
		if (LocalDir.X <= 0.0f) continue; // за спиною камери

		const float HorizontalAngleRad = FMath::Atan2(LocalDir.Y, LocalDir.X);
		const float VerticalAngleRad   = FMath::Atan2(LocalDir.Z, LocalDir.X);
		if (FMath::Abs(HorizontalAngleRad) > HalfHFovRad || FMath::Abs(VerticalAngleRad) > HalfVFovRad)
			continue;

		FString MetadataLine;
		for (const TPair<FString, FString>& Pair : Obj.Metadata)
		{
			if (!MetadataLine.IsEmpty()) MetadataLine += TEXT(", ");
			MetadataLine += FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value);
		}

		UE_LOG(LogUAV, Log, TEXT("CesiumSurroundingsScanner: у кадрі — %s (%s) — %.1f м: %s"),
			*Obj.ActorName, *Obj.ComponentName, Obj.DistanceMeters, *MetadataLine);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Markers — one actor per scanned object, respawned fresh on every Scan().
// ─────────────────────────────────────────────────────────────────────────────

void UCesiumSurroundingsScannerComponent::ClearMarkers()
{
	for (AActor* Marker : SpawnedMarkers)
	{
		if (IsValid(Marker)) Marker->Destroy();
	}
	SpawnedMarkers.Reset();
}

void UCesiumSurroundingsScannerComponent::SpawnMarkerAt(const FVector& WorldLocation)
{
	UWorld* World = GetWorld();
	if (!World) return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (MarkerClass)
	{
		if (AActor* Marker = World->SpawnActor<AActor>(MarkerClass, WorldLocation, FRotator::ZeroRotator, SpawnParams))
			SpawnedMarkers.Add(Marker);
		return;
	}

	// No MarkerClass assigned — fall back to a plain sphere so this works without any setup.
	AStaticMeshActor* Marker = World->SpawnActor<AStaticMeshActor>(WorldLocation, FRotator::ZeroRotator, SpawnParams);
	if (!Marker) return;

	Marker->SetMobility(EComponentMobility::Movable);
	if (UStaticMeshComponent* MeshComp = Marker->GetStaticMeshComponent())
	{
		static UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		MeshComp->SetStaticMesh(SphereMesh);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetWorldScale3D(FVector(MarkerScale));
	}
	SpawnedMarkers.Add(Marker);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan — spherical ray sweep (USensorUtilityLibrary::FindActors), then reads the
// Cesium property table of every hit feature via GetPropertyTableValuesFromHit.
// ─────────────────────────────────────────────────────────────────────────────

const TArray<FCesiumSurroundingObject>& UCesiumSurroundingsScannerComponent::Scan()
{
	LatestScanResults.Reset();
	ClearMarkers();

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

		FString MetadataLine;
		for (const TPair<FString, FString>& Pair : Entry.Metadata)
		{
			if (!MetadataLine.IsEmpty()) MetadataLine += TEXT(", ");
			MetadataLine += FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value);
		}

		UE_LOG(LogUAV, Log, TEXT("CesiumSurroundingsScanner: %s і (%s) — %.1f м від %s: %s"),
			*Entry.ActorName, *Entry.ComponentName, Entry.DistanceMeters, *Owner->GetName(), *MetadataLine);

		if (bSpawnMarkers) SpawnMarkerAt(Entry.HitLocationMeters * 100.0);

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
