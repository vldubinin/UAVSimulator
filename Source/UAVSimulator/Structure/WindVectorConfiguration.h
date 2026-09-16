#pragma once

#include "CoreMinimal.h"
#include "WindVectorConfiguration.generated.h"

/**
 * Один вітровий вектор (початкова точка + кінцева точка + швидкість), як їх експортує
 * Tools/ProjectTools/configurate_env_actors.py у "wind" масив env_actors.json.
 * Джерело правди для AEnvironmentActorManager::WindConfigurations — за тим самим
 * принципом, що й FEWZoneConfiguration для EWConfigurations.
 */
USTRUCT(BlueprintType)
struct FWindVectorConfiguration
{
	GENERATED_BODY()

	/** Широта початку вектора, градуси. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	double StartLatitude = 0.0;

	/** Довгота початку вектора, градуси. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	double StartLongitude = 0.0;

	/** Висота початку вектора, метри. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	double StartHeight = 0.0;

	/** Широта кінця вектора, градуси. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	double EndLatitude = 0.0;

	/** Довгота кінця вектора, градуси. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	double EndLongitude = 0.0;

	/** Висота кінця вектора, метри. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	double EndHeight = 0.0;

	/** Швидкість вітру, м/с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float Speed = 5.0f;

	/** Радіус дії вектора, метри — наскільки паралелепіпед AWindActor розтягується
	 *  вліво/вправо від осі Start->End (конвертується в см при застосуванні до
	 *  AWindActor::Radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float Radius = 50.0f;
};
