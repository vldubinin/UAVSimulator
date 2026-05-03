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
		if (OnSensorDataReady.IsBound())
			BroadcastSensorFrame();
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

	LatestScanResults.Reset();

	const FVector    Origin    = GetComponentLocation();
	const FTransform Transform = GetComponentTransform();

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bTraceComplex = false;

	const float HStep = 360.0f / FMath::Max(HorizontalRays, 1);

	for (int32 V = 0; V < VerticalLayers; ++V)
	{
		const float VAngleRad = FMath::DegreesToRadians(GetVerticalAngle(V));
		const float CosV      = FMath::Cos(VAngleRad);
		const float SinV      = FMath::Sin(VAngleRad);

		for (int32 H = 0; H < HorizontalRays; ++H)
		{
			const float HAngleRad = FMath::DegreesToRadians(H * HStep);

			// Direction in component-local space (X = forward, Y = right, Z = up)
			const FVector LocalDir(
				CosV * FMath::Cos(HAngleRad),
				CosV * FMath::Sin(HAngleRad),
				SinV
			);
			const FVector WorldDir = Transform.TransformVectorNoScale(LocalDir);

			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Origin, Origin + WorldDir * Range, CollisionChannel, QueryParams))
				continue;

			const AActor* HitActor = Hit.GetActor();
			if (!HitActor) continue;

			const FString& Name     = HitActor->GetName();
			const float    Distance = Hit.Distance;

			// Keep the closest hit per actor across all rays
			float* Stored = LatestScanResults.Find(Name);
			if (!Stored)
				LatestScanResults.Add(Name, Distance);
			else if (Distance < *Stored)
				*Stored = Distance;
		}
	}

	return LatestScanResults;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sensor frame serialization
// ─────────────────────────────────────────────────────────────────────────────

void ULidarComponent::BroadcastSensorFrame()
{
	TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	for (const TPair<FString, float>& Pair : LatestScanResults)
		JsonObj->SetNumberField(Pair.Key, Pair.Value);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, Writer);

	FTCHARToUTF8 Utf8(*JsonString);

	FSensorFrame Frame;
	Frame.Topic     = GetSensorTopic();
	Frame.Timestamp = GetWorld()->GetTimeSeconds();
	Frame.Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

	OnSensorDataReady.Broadcast(Frame);
}
