#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "UAVSimulator/Entity/SimulatorMode.h"
#include "UAVSimulator/Entity/OnboardTargetMode.h"
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

	// — Прив'язані віджети (імена мають точно збігатися з Blueprint) ————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> ComboBoxMode;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableText> EditableTextTrajectoryName;

	/** Рядок-контейнер для поля відстані зміщення; прихований у режимі RecordTarget. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> PanelOffsetDistance;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOffsetDistance;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TargetOffsetDistanceText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TrajectoryName;

	/** Обирає, для якої ролі літака (Drone / Target / None) працює бортова камера. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> ComboBoxOnboardCameraMode;

	/** Обирає, для якої ролі літака (Drone / Target / None) працює шина сенсорів. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> ComboBoxSensorsMode;

protected:
	virtual void OnSectionActivated_Implementation() override;

private:
	void PopulateModeComboBox();
	void PopulateOnboardModeComboBoxes();
	void SyncFromGameMode();
	void RefreshOffsetVisibility(ESimulatorMode Mode);
	void RefreshTrajectoryNameVisibility(ESimulatorMode Mode);
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	AUAVSimulatorGameModeBase* GetGameMode() const;

	UFUNCTION() void OnModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void OnTrajectoryNameCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnOffsetDistanceChanged(float Value);
	UFUNCTION() void OnOnboardCameraModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void OnSensorsModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	static FString ModeToString(ESimulatorMode Mode);
	static ESimulatorMode StringToMode(const FString& Str);

	static FString OnboardTargetModeToString(EOnboardTargetMode Mode);
	static EOnboardTargetMode StringToOnboardTargetMode(const FString& Str);

	static const FString ScenarioSaveSlotName;
};
