#include "LidarComponent.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

ULidarComponent::ULidarComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void ULidarComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ScanAccumulator += DeltaTime;
	const float ScanInterval = 1.0f / FMath::Max(ScanRate, 0.1f);
	if (ScanAccumulator >= ScanInterval)
	{
		ScanAccumulator -= ScanInterval;
		Scan();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan
// ─────────────────────────────────────────────────────────────────────────────

float ULidarComponent::GetVerticalAngle(int32 V) const
{
	if (VerticalLayers <= 1) return 0.0f;
	return (-VerticalFOVDeg * 0.5f) + V * (VerticalFOVDeg / (VerticalLayers - 1));
}

const TMap<FString, float>& ULidarComponent::Scan()
{
	UWorld* World = GetWorld();
	if (!World) return LatestScanResults;

	LatestScanTimestamp = World->GetTimeSeconds();

	TArray<FHitResult> FHitResults = USensorUtilityLibrary::FindActors(
		this,                    // Контекст для отримання World
		GetComponentTransform(), // Початкова точка та ротація лідара
		GetOwner(),              // Ігноруємо сам дрон
		Range,
		HorizontalRays,
		VerticalLayers,
		VerticalFOVDeg,
		CollisionChannel
	);

	for (FHitResult Hit : FHitResults)
	{
		const AActor* HitActor = Hit.GetActor();

		const FString& Name = HitActor->GetName();
		const float    Distance = Hit.Distance;

		float* Stored = LatestScanResults.Find(Name);
		if (!Stored)
		{
			LatestScanResults.Add(Name, Distance);
		}
		else if (Distance < *Stored)
		{
			*Stored = Distance;
		}
	}

	return LatestScanResults;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — called on game thread by SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool ULidarComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (LatestScanResults.IsEmpty()) return false;

	TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	for (const TPair<FString, float>& Pair : LatestScanResults)
		JsonObj->SetNumberField(Pair.Key, Pair.Value);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, Writer);

	FTCHARToUTF8 Utf8(*JsonString);

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestScanTimestamp;
	OutFrame.Payload.Reset();
	OutFrame.Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return true;
}
