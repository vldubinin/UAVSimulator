#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "SyntheticDataSectionWidget.generated.h"

class UButton;
class UEditableTextBox;
class ADroneDatasetGeneratorActor;
class ADroneKeyPointDatasetActor;

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

	virtual void OnSectionActivated_Implementation() override;

private:
	void SyncFromActors();

	ADroneDatasetGeneratorActor*  GetDatasetActor()  const;
	ADroneKeyPointDatasetActor*   GetKeyPointActor() const;

	UFUNCTION() void OnRunSphericalContourClicked();
	UFUNCTION() void OnRunKPointDetectionClicked();
	UFUNCTION() void OnSphericalContourPathCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION() void OnKPointDetectionPathCommitted(const FText& Text, ETextCommit::Type CommitType);
};
