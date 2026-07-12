#include "DroneKeyPointDatasetActor.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

ADroneKeyPointDatasetActor::ADroneKeyPointDatasetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────

void ADroneKeyPointDatasetActor::ExportKeyPoints()
{
	if (!DroneBlueprintClass)
	{
		/* UE_LOG(LogUAV, Warning, TEXT("KeyPointDataset: DroneBlueprintClass is not set.")); */
		return;
	}
	if (OutputJsonPath.IsEmpty())
	{
		/* UE_LOG(LogUAV, Warning, TEXT("KeyPointDataset: OutputJsonPath is empty.")); */
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
		/* UE_LOG(LogUAV, Error, TEXT("KeyPointDataset: Failed to spawn drone Blueprint.")); */
		return;
	}

	// ── Collect keypoint components ───────────────────────────────────────────
	TArray<UKeyPointComponent*> KPComps;
	Drone->GetComponents<UKeyPointComponent>(KPComps);

	if (KPComps.IsEmpty())
	{
		/* UE_LOG(LogUAV, Warning,
			TEXT("KeyPointDataset: Drone has no UKeyPointComponent instances — nothing to export.")); */
		Drone->Destroy();
		return;
	}

	/* UE_LOG(LogUAV, Log, TEXT("KeyPointDataset: Found %d keypoints on %s."),
		KPComps.Num(), *DroneBlueprintClass->GetName()); */

	// ── Compute normalisation scale ───────────────────────────────────────────
	// Transform every keypoint to drone-local space and find the largest absolute
	// coordinate value. Dividing by this value maps everything into [-1, 1].
	const FTransform DroneTransform = Drone->GetActorTransform();
	float MaxAbsCoord = KINDA_SMALL_NUMBER;

	for (const UKeyPointComponent* KP : KPComps)
	{
		if (!IsValid(KP)) continue;
		const FVector Local = DroneTransform.InverseTransformPosition(KP->GetComponentLocation());
		MaxAbsCoord = FMath::Max(MaxAbsCoord, FMath::Abs(Local.X));
		MaxAbsCoord = FMath::Max(MaxAbsCoord, FMath::Abs(Local.Y));
		MaxAbsCoord = FMath::Max(MaxAbsCoord, FMath::Abs(Local.Z));
	}

	// ── Serialise and write ───────────────────────────────────────────────────
	const FString ModelName = DroneBlueprintClass->GetName();
	const FString JsonStr   = BuildJson(ModelName, MaxAbsCoord, KPComps, DroneTransform);

	Drone->Destroy();

	if (FFileHelper::SaveStringToFile(JsonStr, *OutputJsonPath))
	{
		/* UE_LOG(LogUAV, Log, TEXT("KeyPointDataset: Saved %d keypoints → %s"),
			KPComps.Num(), *OutputJsonPath); */
	}
	else
	{
		/* UE_LOG(LogUAV, Error, TEXT("KeyPointDataset: Failed to write %s"), *OutputJsonPath); */
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialisation
// ─────────────────────────────────────────────────────────────────────────────

FString ADroneKeyPointDatasetActor::BuildJson(
	const FString& ModelName, float ScaleCm,
	const TArray<UKeyPointComponent*>& KPComps,
	const FTransform& DroneTransform) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("drone_model"), ModelName);
	Root->SetNumberField(TEXT("scale_cm"),    ScaleCm);

	TArray<TSharedPtr<FJsonValue>> KPArray;
	for (const UKeyPointComponent* KP : KPComps)
	{
		if (!IsValid(KP)) continue;

		const FVector Local      = DroneTransform.InverseTransformPosition(KP->GetComponentLocation());
		const FVector Normalised = Local / ScaleCm;

		TSharedPtr<FJsonObject> KPObj = MakeShared<FJsonObject>();
		KPObj->SetStringField(TEXT("id"), KP->PointID);
		KPObj->SetNumberField(TEXT("x"),  Normalised.X);
		KPObj->SetNumberField(TEXT("y"),  Normalised.Y);
		KPObj->SetNumberField(TEXT("z"),  Normalised.Z);
		KPArray.Add(MakeShared<FJsonValueObject>(KPObj));
	}

	Root->SetArrayField(TEXT("keypoints"), KPArray);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
