#include "SensorUtilityLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"

TArray<FHitResult> USensorUtilityLibrary::FindActors(const UObject* WorldContextObject,
	const FTransform& OriginTransform,
	const AActor* ActorToIgnore,
	float Range,
	int32 HorizontalRays,
	int32 VerticalLayers,
	float VerticalFOVDeg,
	ECollisionChannel CollisionChannel)
{
	TArray<FHitResult> ScanResults;

	// Отримуємо світ через контекстний об'єкт
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World) return ScanResults;

	const FVector Origin = OriginTransform.GetLocation();

	FCollisionQueryParams QueryParams;
	if (ActorToIgnore)
	{
		QueryParams.AddIgnoredActor(ActorToIgnore);
	}
	QueryParams.bTraceComplex = false;

	const float HStep = 360.0f / FMath::Max(HorizontalRays, 1);

	for (int32 V = 0; V < VerticalLayers; ++V)
	{
		// Обчислення вертикального кута (раніше було у GetVerticalAngle)
		float VerticalAngle = 0.0f;
		if (VerticalLayers > 1)
		{
			VerticalAngle = (-VerticalFOVDeg * 0.5f) + V * (VerticalFOVDeg / (VerticalLayers - 1));
		}

		const float VAngleRad = FMath::DegreesToRadians(VerticalAngle);
		const float CosV = FMath::Cos(VAngleRad);
		const float SinV = FMath::Sin(VAngleRad);

		for (int32 H = 0; H < HorizontalRays; ++H)
		{
			const float HAngleRad = FMath::DegreesToRadians(H * HStep);

			// Напрямок у локальному просторі (X = forward, Y = right, Z = up)
			const FVector LocalDir(
				CosV * FMath::Cos(HAngleRad),
				CosV * FMath::Sin(HAngleRad),
				SinV
			);
			const FVector WorldDir = OriginTransform.TransformVectorNoScale(LocalDir);

			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Origin, Origin + WorldDir * Range, CollisionChannel, QueryParams))
				continue;

			if (!Hit.GetActor()) continue;

			ScanResults.Add(Hit);
		}
	} // Кінець зовнішнього циклу

	// Повертаємо результат ТІЛЬКИ після завершення всіх циклів
	return ScanResults;
}