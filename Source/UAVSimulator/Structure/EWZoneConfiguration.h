#pragma once

#include "CoreMinimal.h"
#include "EWZoneConfiguration.generated.h"

/**
 * Одна конфігурація зони РЕБ (позиція + радіус), як їх експортує
 * Tools/ProjectTools/configurate_env_actors.py у "electronic_warfare" масив
 * env_actors.json. Джерело правди для AEnvironmentActorManager::EWConfigurations.
 */
USTRUCT(BlueprintType)
struct FEWZoneConfiguration
{
	GENERATED_BODY()

	/** Широта, градуси. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW Zone")
	double Latitude = 0.0;

	/** Довгота, градуси. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW Zone")
	double Longitude = 0.0;

	/** Висота, метри. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW Zone")
	double Height = 0.0;

	/** Радіус дії, метри (конвертується в см при застосуванні до AEWZoneActor::Radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW Zone")
	float Radius = 50.0f;
};
