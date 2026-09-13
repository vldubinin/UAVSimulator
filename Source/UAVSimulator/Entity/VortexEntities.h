#pragma once

#include "CoreMinimal.h"
#include "VortexEntities.generated.h"

/**
 * Приєднаний (bound) відрізок вихору, розташований на лінії 1/4 хорди панелі крила.
 * Представляє нитку, що несе циркуляцію, у моделі LLT/VLM.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FBoundVortex
{
	GENERATED_BODY()

	/** Початкова точка нитки вихору у світових координатах (напр. кінець біля кореня крила). */
	UPROPERTY(BlueprintReadOnly)
	FVector StartPoint;

	/** Кінцева точка нитки вихору у світових координатах (напр. кінець біля кінчика крила). */
	UPROPERTY(BlueprintReadOnly)
	FVector EndPoint;

	/** Сила циркуляції Γ (м²/с). Додатне значення — проти годинникової стрілки, якщо дивитися від кінчика до кореня крила. */
	UPROPERTY(BlueprintReadOnly)
	float Gamma;

	FBoundVortex()
		: StartPoint(FVector::ZeroVector)
		, EndPoint(FVector::ZeroVector)
		, Gamma(0.0f)
	{}

	FBoundVortex(FVector InStart, FVector InEnd, float InGamma)
		: StartPoint(InStart)
		, EndPoint(InEnd)
		, Gamma(InGamma)
	{}
};

/**
 * Один вузол у лінії сходового (trailing) вихрового сліду, що сходить з краю панелі крила.
 * Вузли зберігаються послідовно для кожної лінії сліду; сусідні вузли визначають відрізки вихору.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FTrailingVortexNode
{
	GENERATED_BODY()

	/** Позиція цього вузла сліду у світових координатах. */
	UPROPERTY(BlueprintReadOnly)
	FVector Position;

	/** Циркуляція Γ (м²/с), яку несе сходова нитка, що покидає крило в момент створення цього вузла. */
	UPROPERTY(BlueprintReadOnly)
	float Gamma;

	FTrailingVortexNode()
		: Position(FVector::ZeroVector)
		, Gamma(0.0f)
	{}

	FTrailingVortexNode(FVector InPosition, float InGamma)
		: Position(InPosition)
		, Gamma(InGamma)
	{}
};
