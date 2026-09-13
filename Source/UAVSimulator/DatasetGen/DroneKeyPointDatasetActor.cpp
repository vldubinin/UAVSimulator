#include "DroneKeyPointDatasetActor.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"

// ─────────────────────────────────────────────────────────────────────────────
// Конструювання
// ─────────────────────────────────────────────────────────────────────────────

ADroneKeyPointDatasetActor::ADroneKeyPointDatasetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Публічна точка входу
// ─────────────────────────────────────────────────────────────────────────────

void ADroneKeyPointDatasetActor::ExportKeyPoints()
{
	if (!DroneBlueprintClass)
	{
		return;
	}
	if (OutputJsonPath.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// ── Спавн дрона ───────────────────────────────────────────────────────────
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Drone = World->SpawnActor<AActor>(
		DroneBlueprintClass, GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!Drone)
	{
		return;
	}

	// ── Збір компонентів ключових точок ───────────────────────────────────────
	TArray<UKeyPointComponent*> KPComps;
	Drone->GetComponents<UKeyPointComponent>(KPComps);

	if (KPComps.IsEmpty())
	{
		Drone->Destroy();
		return;
	}

	// ── Обчислення масштабу нормалізації ──────────────────────────────────────
	// Переводимо кожну ключову точку в локальний простір дрона і знаходимо найбільше
	// за модулем значення координати. Ділення на нього відображає все в [-1, 1].
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

	// ── Серіалізація та запис ─────────────────────────────────────────────────
	const FString ModelName = DroneBlueprintClass->GetName();
	const FString JsonStr   = BuildJson(ModelName, MaxAbsCoord, KPComps, DroneTransform);

	Drone->Destroy();

	FFileHelper::SaveStringToFile(JsonStr, *OutputJsonPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// Серіалізація в JSON
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
