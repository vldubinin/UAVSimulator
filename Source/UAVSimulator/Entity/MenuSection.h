#pragma once

#include "CoreMinimal.h"
#include "MenuSection.generated.h"

UENUM(BlueprintType)
enum class EMenuSection : uint8
{
	Scenario    UMETA(DisplayName = "Сценарій"),
	Sensors     UMETA(DisplayName = "Сенсори"),
	Environment UMETA(DisplayName = "Оточення"),
	SyntheticData UMETA(DisplayName = "Синтетичні дані"),
};
