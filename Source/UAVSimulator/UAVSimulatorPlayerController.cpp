#include "UAVSimulatorPlayerController.h"
#include "UAVSimulatorGameModeBase.h"
#include "UAVSimulator/UI/SimulatorMenuWidget.h"
#include "UAVSimulator/Entity/MenuSection.h"
#include "Blueprint/UserWidget.h"

void AUAVSimulatorPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!MenuWidgetClass) return;

	MenuWidget = CreateWidget<USimulatorMenuWidget>(this, MenuWidgetClass);
	if (MenuWidget)
	{
		MenuWidget->AddToViewport();
		bShowMouseCursor = true;
		SetInputMode(FInputModeGameAndUI());
	}
}

void AUAVSimulatorPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AUAVSimulatorPlayerController::OnQPressed);
}

void AUAVSimulatorPlayerController::ShowMenu()
{
	if (!MenuWidget) return;
	MenuWidget->SetVisibility(ESlateVisibility::Visible);
	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());
}

void AUAVSimulatorPlayerController::HideMenu()
{
	if (!MenuWidget) return;
	MenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

void AUAVSimulatorPlayerController::ToggleMenu()
{
	if (IsMenuVisible()) HideMenu(); else ShowMenu();
}

bool AUAVSimulatorPlayerController::IsMenuVisible() const
{
	return MenuWidget && MenuWidget->GetVisibility() == ESlateVisibility::Visible;
}

void AUAVSimulatorPlayerController::RegisterCameraWidget(UUserWidget* Widget)
{
	CameraWidgetRef = Widget;
}

void AUAVSimulatorPlayerController::RemoveCameraWidgets()
{
	if (CameraWidgetRef)
	{
		CameraWidgetRef->RemoveFromParent();
		CameraWidgetRef = nullptr;
	}
}

void AUAVSimulatorPlayerController::OnQPressed()
{
	RemoveCameraWidgets();

	if (AUAVSimulatorGameModeBase* GM = Cast<AUAVSimulatorGameModeBase>(GetWorld()->GetAuthGameMode()))
	{
		GM->StopSimulation();
	}

	ShowMenu();

	if (MenuWidget)
	{
		MenuWidget->OpenSection(EMenuSection::Scenario);
	}
}
