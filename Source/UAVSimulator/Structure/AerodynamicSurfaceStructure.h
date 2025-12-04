#pragma once

#include "CoreMinimal.h" 
#include "Engine/DataTable.h"
#include "Runtime/Core/Public/Math/Vector.h"
#include "UAVSimulator/Entity/FlapType.h"


#include "AerodynamicSurfaceStructure.generated.h" 


USTRUCT(BlueprintType)
struct FAerodynamicSurfaceStructure : public FTableRowBase
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Розмір хорди"))
		float ChordSize = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Зміщення хорди"))
		FVector Offset = FVector(0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Початкове зміщення закрилки"))
		float StartFlapPosition = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Кінцеве зміщення закрилки"))
		float EndFlapPosition = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Максимальний кут закрилки"))
		int MaxFlapAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Мінімальний кут закрилки"))
		int MinFlapAngle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aerodynamics")
		UDataTable* AerodynamicTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Тип керуючої поверхні"))
		EFlapType FlapType;
};
