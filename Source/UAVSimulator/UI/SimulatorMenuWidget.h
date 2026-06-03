#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UAVSimulator/Entity/MenuSection.h"
#include "SimulatorMenuWidget.generated.h"

class UButton;
class UWidgetSwitcher;
class USimulatorSectionWidget;
class AUAVSimulatorGameModeBase;
class AUAVSimulatorPlayerController;

UCLASS()
class UAVSIMULATOR_API USimulatorMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Switches to a menu section and notifies the active panel. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OpenSection(EMenuSection Section);

	UPROPERTY(BlueprintReadOnly, Category = "Menu")
	EMenuSection ActiveSection = EMenuSection::Scenario;

protected:
	virtual void NativeConstruct() override;

	// Navigation buttons — must exist with these exact names in the Blueprint widget.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ButtonScenario;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ButtonSensors;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ButtonEnvironment;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ButtonSyntheticData;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ButtonStartSimulation;

	// Switcher — children must be ordered: 0=Scenario, 1=Sensors, 2=Environment, 3=SyntheticData.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> ContentSwitcher;

private:
	UFUNCTION() void OnScenarioClicked();
	UFUNCTION() void OnSensorsClicked();
	UFUNCTION() void OnEnvironmentClicked();
	UFUNCTION() void OnSyntheticDataClicked();
	UFUNCTION() void OnStartSimulationClicked();

	void NotifySectionChange(EMenuSection NewSection);
	AUAVSimulatorGameModeBase* GetGameMode() const;
};
