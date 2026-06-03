#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SimulatorSectionWidget.generated.h"

UCLASS(Abstract)
class UAVSIMULATOR_API USimulatorSectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Menu")
	void OnSectionActivated();
	virtual void OnSectionActivated_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Menu")
	void OnSectionDeactivated();
	virtual void OnSectionDeactivated_Implementation();
};
