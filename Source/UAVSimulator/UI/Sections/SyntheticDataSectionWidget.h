#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "SyntheticDataSectionWidget.generated.h"

class UCheckBox;
class UButton;
class UEditableTextBox;
class ADroneDatasetGeneratorActor;
class ADroneKeyPointDatasetActor;
class ASceneObjectDatasetActor;
class AYoloMarkerDatasetActor;
class AUAVSimulatorGameModeBase;

UCLASS()
class UAVSIMULATOR_API USyntheticDataSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RunSphericalContourBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> SphericalContourFilePathTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RunKPointDetectionBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> KPointDetectionBtnTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> SegmentationMaskCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> BBoxDetectionCB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RunSceneObjectExportBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> SceneObjectExportPathTextBox;

	// Optional — add these widgets to the menu Blueprint to expose the YOLO marker
	// dataset generator. Absent bindings are tolerated so the existing UMG still loads.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RunMarkerDatasetBtn;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> MarkerDatasetPathTextBox;

	virtual void OnSectionActivated_Implementation() override;

private:
	void SyncFromActors();
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	ADroneDatasetGeneratorActor*  GetDatasetActor()  const;
	ADroneKeyPointDatasetActor*   GetKeyPointActor() const;
	ASceneObjectDatasetActor*     GetSceneObjectActor() const;
	AYoloMarkerDatasetActor*      GetMarkerDatasetActor() const;
	AUAVSimulatorGameModeBase* GetGameMode() const;

	UFUNCTION() void OnRunSphericalContourClicked();
	UFUNCTION() void OnRunKPointDetectionClicked();
	UFUNCTION() void OnRunSceneObjectExportClicked();
	UFUNCTION() void OnRunMarkerDatasetClicked();
	UFUNCTION() void OnSphericalContourPathCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnKPointDetectionPathCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnSceneObjectExportPathCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnMarkerDatasetPathCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnSegmentationMaskChanged(bool bIsChecked);
	UFUNCTION() void OnBBoxDetectionChanged(bool bIsChecked);

	static const FString SyntheticDataSaveSlotName;
};
