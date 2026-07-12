#include "ScenarioSectionWidget.h"
#include "UAVSimulator/UAVSimulatorGameModeBase.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Save/ScenarioSettingsSave.h"

const FString UScenarioSectionWidget::ScenarioSaveSlotName = TEXT("ScenarioSettings");

void UScenarioSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	PopulateModeComboBox();
	LoadAndApplySavedSettings();
	SyncFromGameMode();

	ComboBoxMode->OnSelectionChanged.AddDynamic(this, &UScenarioSectionWidget::OnModeSelectionChanged);
	EditableTextTrajectoryName->OnTextCommitted.AddDynamic(this, &UScenarioSectionWidget::OnTrajectoryNameCommitted);
	SpinBoxOffsetDistance->OnValueChanged.AddDynamic(this, &UScenarioSectionWidget::OnOffsetDistanceChanged);
}

void UScenarioSectionWidget::PopulateModeComboBox()
{
	ComboBoxMode->ClearOptions();
	ComboBoxMode->AddOption(ModeToString(ESimulatorMode::RecordTarget));
	ComboBoxMode->AddOption(ModeToString(ESimulatorMode::PlaybackAndTrack));
	ComboBoxMode->AddOption(ModeToString(ESimulatorMode::PlaybackAndAutoTrack));
	ComboBoxMode->AddOption(ModeToString(ESimulatorMode::Playback));
	ComboBoxMode->AddOption(ModeToString(ESimulatorMode::Free));
}

void UScenarioSectionWidget::SyncFromGameMode()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM) return;

	ComboBoxMode->SetSelectedOption(ModeToString(GM->CurrentSimulatorMode));
	EditableTextTrajectoryName->SetText(FText::FromString(GM->ScenarioSlotName));
	SpinBoxOffsetDistance->SetValue(GM->TargetSpawnOffsetDistance);
	RefreshOffsetVisibility(GM->CurrentSimulatorMode);
}

void UScenarioSectionWidget::RefreshOffsetVisibility(ESimulatorMode Mode)
{
	const bool bVisible = Mode == ESimulatorMode::PlaybackAndTrack || Mode == ESimulatorMode::Playback;
	PanelOffsetDistance->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	TargetOffsetDistanceText->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UScenarioSectionWidget::OnModeSelectionChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM) return;

	const ESimulatorMode NewMode = StringToMode(SelectedItem);
	GM->CurrentSimulatorMode = NewMode;
	RefreshOffsetVisibility(NewMode);
	SaveCurrentSettings();
}

void UScenarioSectionWidget::OnTrajectoryNameCommitted(const FText& Text, ETextCommit::Type /*CommitType*/)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		GM->ScenarioSlotName = Text.ToString();
	}
	SaveCurrentSettings();
}

void UScenarioSectionWidget::OnOffsetDistanceChanged(float Value)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		GM->TargetSpawnOffsetDistance = Value;
	}
	SaveCurrentSettings();
}

void UScenarioSectionWidget::OnSectionActivated_Implementation()
{
	SyncFromGameMode();
}

void UScenarioSectionWidget::LoadAndApplySavedSettings()
{
	UScenarioSettingsSave* Save = Cast<UScenarioSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(ScenarioSaveSlotName, /*UserIndex=*/0));
	if (!Save)
		return;

	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM)
		return;

	GM->CurrentSimulatorMode      = Save->CurrentSimulatorMode;
	GM->ScenarioSlotName          = Save->ScenarioSlotName;
	GM->TargetSpawnOffsetDistance = Save->TargetSpawnOffsetDistance;
}

void UScenarioSectionWidget::SaveCurrentSettings()
{
	AUAVSimulatorGameModeBase* GM = GetGameMode();
	if (!GM)
		return;

	UScenarioSettingsSave* Save = Cast<UScenarioSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UScenarioSettingsSave::StaticClass()));

	Save->CurrentSimulatorMode      = GM->CurrentSimulatorMode;
	Save->ScenarioSlotName          = GM->ScenarioSlotName;
	Save->TargetSpawnOffsetDistance = GM->TargetSpawnOffsetDistance;

	UGameplayStatics::SaveGameToSlot(Save, ScenarioSaveSlotName, /*UserIndex=*/0);
}

AUAVSimulatorGameModeBase* UScenarioSectionWidget::GetGameMode() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<AUAVSimulatorGameModeBase>(World->GetAuthGameMode());
	}
	return nullptr;
}

FString UScenarioSectionWidget::ModeToString(ESimulatorMode Mode)
{
	switch (Mode)
	{
	case ESimulatorMode::RecordTarget:     return TEXT("Record Target");
	case ESimulatorMode::PlaybackAndTrack: return TEXT("Playback and Track");
	case ESimulatorMode::Playback:         return TEXT("Playback");
	case ESimulatorMode::PlaybackAndAutoTrack:         return TEXT("Playback and Auto Track");
	case ESimulatorMode::Free:              return TEXT("Free");
	}
	return TEXT("Record Target");
}

ESimulatorMode UScenarioSectionWidget::StringToMode(const FString& Str)
{
	if (Str == TEXT("Playback and Track")) return ESimulatorMode::PlaybackAndTrack;
	if (Str == TEXT("Playback"))           return ESimulatorMode::Playback;
	if (Str == TEXT("Playback and Auto Track"))           return ESimulatorMode::PlaybackAndAutoTrack;
	if (Str == TEXT("Free"))               return ESimulatorMode::Free;
	return ESimulatorMode::RecordTarget;
}
