#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "UAVSimulator/Entity/SimulatorMode.h"
#include "ScenarioSectionWidget.generated.h"

class UComboBoxString;
class UEditableText;
class USpinBox;
class UTextBlock;
class UWidget;
class AUAVSimulatorGameModeBase;

UCLASS()
class UAVSIMULATOR_API UScenarioSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	// — Bound widgets (names must match exactly in the Blueprint) ————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> ComboBoxMode;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableText> EditableTextTrajectoryName;

	/** Container row for the offset distance field; hidden when mode is RecordTarget. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> PanelOffsetDistance;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOffsetDistance;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TargetOffsetDistanceText;

protected:
	virtual void OnSectionActivated_Implementation() override;

private:
	void PopulateModeComboBox();
	void SyncFromGameMode();
	void RefreshOffsetVisibility(ESimulatorMode Mode);
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	AUAVSimulatorGameModeBase* GetGameMode() const;

	UFUNCTION() void OnModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void OnTrajectoryNameCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnOffsetDistanceChanged(float Value);

	static FString ModeToString(ESimulatorMode Mode);
	static ESimulatorMode StringToMode(const FString& Str);

	static const FString ScenarioSaveSlotName;
};
