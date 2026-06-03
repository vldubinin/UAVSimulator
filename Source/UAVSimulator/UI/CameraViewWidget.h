#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CameraViewWidget.generated.h"

class AAirplane;

UCLASS()
class UAVSIMULATOR_API UCameraViewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetAirplane(AAirplane* InAirplane);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	TObjectPtr<AAirplane> Airplane;
};
