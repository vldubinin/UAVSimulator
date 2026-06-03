#include "SimulatorMenuWidget.h"
#include "UAVSimulator/UI/Sections/SimulatorSectionWidget.h"
#include "UAVSimulator/UAVSimulatorGameModeBase.h"
#include "UAVSimulator/UAVSimulatorPlayerController.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Kismet/GameplayStatics.h"

void USimulatorMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ButtonScenario->OnClicked.AddDynamic(this, &USimulatorMenuWidget::OnScenarioClicked);
	ButtonSensors->OnClicked.AddDynamic(this, &USimulatorMenuWidget::OnSensorsClicked);
	ButtonEnvironment->OnClicked.AddDynamic(this, &USimulatorMenuWidget::OnEnvironmentClicked);
	ButtonSyntheticData->OnClicked.AddDynamic(this, &USimulatorMenuWidget::OnSyntheticDataClicked);
	ButtonStartSimulation->OnClicked.AddDynamic(this, &USimulatorMenuWidget::OnStartSimulationClicked);

	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		ButtonStartSimulation->SetIsEnabled(!GM->bSimulationStarted);
	}

	OpenSection(EMenuSection::Scenario);
}

void USimulatorMenuWidget::OpenSection(EMenuSection Section)
{
	NotifySectionChange(Section);
	ActiveSection = Section;
	ContentSwitcher->SetActiveWidgetIndex((int32)Section);
}

void USimulatorMenuWidget::NotifySectionChange(EMenuSection NewSection)
{
	if (UWidget* OldPanel = ContentSwitcher->GetActiveWidget())
	{
		if (USimulatorSectionWidget* OldSection = Cast<USimulatorSectionWidget>(OldPanel))
		{
			OldSection->OnSectionDeactivated();
		}
	}

	if (UWidget* NewPanel = ContentSwitcher->GetWidgetAtIndex((int32)NewSection))
	{
		if (USimulatorSectionWidget* NewSectionWidget = Cast<USimulatorSectionWidget>(NewPanel))
		{
			NewSectionWidget->OnSectionActivated();
		}
	}
}

void USimulatorMenuWidget::OnScenarioClicked()    { OpenSection(EMenuSection::Scenario); }
void USimulatorMenuWidget::OnSensorsClicked()     { OpenSection(EMenuSection::Sensors); }
void USimulatorMenuWidget::OnEnvironmentClicked() { OpenSection(EMenuSection::Environment); }
void USimulatorMenuWidget::OnSyntheticDataClicked() { OpenSection(EMenuSection::SyntheticData); }

void USimulatorMenuWidget::OnStartSimulationClicked()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM) return;

	GM->StartSimulation();
	ButtonStartSimulation->SetIsEnabled(false);

	if (AUAVSimulatorPlayerController* PC = Cast<AUAVSimulatorPlayerController>(GetOwningPlayer()))
	{
		PC->HideMenu();
	}
}

AUAVSimulatorGameModeBase* USimulatorMenuWidget::GetGameMode() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<AUAVSimulatorGameModeBase>(World->GetAuthGameMode());
	}
	return nullptr;
}
