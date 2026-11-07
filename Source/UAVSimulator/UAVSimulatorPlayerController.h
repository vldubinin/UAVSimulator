#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UAVSimulatorPlayerController.generated.h"

class USimulatorMenuWidget;

UCLASS()
class UAVSIMULATOR_API AUAVSimulatorPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Клас Blueprint-віджета, який інстанціюється як меню симулятора. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<USimulatorMenuWidget> MenuWidgetClass;

	UFUNCTION(BlueprintCallable, Category = "UI")
	void ShowMenu();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void HideMenu();

	UFUNCTION(BlueprintCallable, Category = "UI")
	void ToggleMenu();

	UFUNCTION(BlueprintPure, Category = "UI")
	bool IsMenuVisible() const;

	/** Старт симуляції + сховати меню (як кнопка Start). Використовує UTestSessionSubsystem (команда START). */
	UFUNCTION(BlueprintCallable, Category = "Simulator")
	void StartSimulationAndHideMenu();

	/** Зупинка симуляції + меню на секції Scenario (як Q). Використовує і Q, і UTestSessionSubsystem (STOP). */
	UFUNCTION(BlueprintCallable, Category = "Simulator")
	void StopSimulationAndShowMenu();

	/** Викликається з AAirplane при створенні віджета камери, щоб PC міг його прибрати. */
	void RegisterCameraWidget(UUserWidget* Widget);
	void RemoveCameraWidgets();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	UPROPERTY()
	TObjectPtr<USimulatorMenuWidget> MenuWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> CameraWidgetRef;

	UFUNCTION() void OnQPressed();
};
