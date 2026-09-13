#pragma once

#include "CoreMinimal.h"
#include "CesiumSurroundingObject.generated.h"

/**
 * Один об'єкт, знайдений скануванням UCesiumSurroundingsScannerComponent: актор/компонент,
 * якому він належить, відстань від сканувального актора, і будь-які властивості Cesium-метаданих
 * (напр. Longitude/Latitude/Height, налаштовані за
 * https://cesium.com/learn/unreal/unreal-visualize-metadata), які містить його таблиця властивостей.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FCesiumSurroundingObject
{
	GENERATED_BODY()

	/** Стабільний ідентифікатор цього об'єкта (UCesiumSurroundingsScannerComponent::BuildFeatureKey), однаковий у різних кадрах. */
	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	FString ObjectID;

	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	FString ComponentName;

	/** Відстань від сканувального актора, в метрах. */
	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	float DistanceMeters = 0.0f;

	/** Точка влучання у світових координатах, в метрах (конвертовано з сантиметрів Unreal). */
	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	FVector HitLocationMeters = FVector::ZeroVector;

	/** Значення таблиці властивостей для знайденого об'єкта, за ключем — назвою властивості (напр. "Longitude", "Latitude", "Height"). */
	UPROPERTY(BlueprintReadOnly, Category = "Cesium")
	TMap<FString, FString> Metadata;
};
