#include "SceneObjectDatasetActor.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Actor/Airplane.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

ASceneObjectDatasetActor::ASceneObjectDatasetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────

void ASceneObjectDatasetActor::ExportSceneObjects()
{
	if (OutputJsonPath.IsEmpty())
	{
		/* UE_LOG(LogUAV, Warning, TEXT("SceneObjectDataset: OutputJsonPath is empty.")); */
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// ── Collect scene objects ─────────────────────────────────────────────────
	// Player and target drones are both AAirplane instances, so a single class
	// check excludes them without needing any tag/marker on the actors.
	TArray<AActor*> Objects;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor)) continue;
		if (Actor == this) continue;
		if (Actor->IsA<AAirplane>()) continue;
		if (IsExcludedClass(Actor)) continue;
		if (!Actor->FindComponentByClass<UStaticMeshComponent>()) continue;

		Objects.Add(Actor);
	}

	const FString JsonStr = BuildJson(Objects);

	if (FFileHelper::SaveStringToFile(JsonStr, *OutputJsonPath))
	{
		/* UE_LOG(LogUAV, Log, TEXT("SceneObjectDataset: Saved %d objects → %s"),
			Objects.Num(), *OutputJsonPath); */
	}
	else
	{
		/* UE_LOG(LogUAV, Error, TEXT("SceneObjectDataset: Failed to write %s"), *OutputJsonPath); */
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Exclusion filtering
// ─────────────────────────────────────────────────────────────────────────────

bool ASceneObjectDatasetActor::IsExcludedClass(const AActor* Actor) const
{
	const FString ClassName = Actor->GetClass()->GetName();
	for (const FString& Excluded : ExcludedActorClassNames)
	{
		if (ClassName.Equals(Excluded, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialisation
// ─────────────────────────────────────────────────────────────────────────────

FString ASceneObjectDatasetActor::BuildJson(const TArray<AActor*>& Objects) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> ObjArray;
	for (const AActor* Actor : Objects)
	{
		const FVector Location = Actor->GetActorLocation();
		const FVector Size     = Actor->GetComponentsBoundingBox(/*bNonColliding=*/true).GetSize();

		TSharedPtr<FJsonObject> ObjJson = MakeShared<FJsonObject>();
		ObjJson->SetStringField(TEXT("name"),  Actor->GetActorNameOrLabel());
		ObjJson->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ObjJson->SetNumberField(TEXT("x"), Location.X);
		ObjJson->SetNumberField(TEXT("y"), Location.Y);
		ObjJson->SetNumberField(TEXT("z"), Location.Z);
		ObjJson->SetNumberField(TEXT("size_x"), Size.X);
		ObjJson->SetNumberField(TEXT("size_y"), Size.Y);
		ObjJson->SetNumberField(TEXT("size_z"), Size.Z);
		ObjArray.Add(MakeShared<FJsonValueObject>(ObjJson));
	}

	Root->SetArrayField(TEXT("objects"), ObjArray);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
