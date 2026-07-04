#include "SyntheticDataSectionWidget.h"
#include "UAVSimulator/DatasetGen/DroneDatasetGeneratorActor.h"
#include "UAVSimulator/DatasetGen/DroneKeyPointDatasetActor.h"
#include "UAVSimulator/DatasetGen/SceneObjectDatasetActor.h"
#include "UAVSimulator/UAVSimulatorGameModeBase.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

void USyntheticDataSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SyncFromActors();

	SegmentationMaskCB->OnCheckStateChanged.AddDynamic(this, &USyntheticDataSectionWidget::OnSegmentationMaskChanged);
	BBoxDetectionCB->OnCheckStateChanged.AddDynamic(this, &USyntheticDataSectionWidget::OnBBoxDetectionChanged);
	RunSphericalContourBtn->OnClicked.AddDynamic(this, &USyntheticDataSectionWidget::OnRunSphericalContourClicked);
	RunKPointDetectionBtn->OnClicked.AddDynamic(this, &USyntheticDataSectionWidget::OnRunKPointDetectionClicked);
	RunSceneObjectExportBtn->OnClicked.AddDynamic(this, &USyntheticDataSectionWidget::OnRunSceneObjectExportClicked);
	SphericalContourFilePathTextBox->OnTextCommitted.AddDynamic(this, &USyntheticDataSectionWidget::OnSphericalContourPathCommitted);
	KPointDetectionBtnTextBox->OnTextCommitted.AddDynamic(this, &USyntheticDataSectionWidget::OnKPointDetectionPathCommitted);
	SceneObjectExportPathTextBox->OnTextCommitted.AddDynamic(this, &USyntheticDataSectionWidget::OnSceneObjectExportPathCommitted);
}

void USyntheticDataSectionWidget::OnSectionActivated_Implementation()
{
	SyncFromActors();
}

void USyntheticDataSectionWidget::SyncFromActors()
{
	if (ADroneDatasetGeneratorActor* Actor = GetDatasetActor())
	{
		// Show the base folder (strip the filename to display just the directory)
		const FString BasePath = FPaths::GetPath(Actor->OutputJsonPath);
		SphericalContourFilePathTextBox->SetText(FText::FromString(BasePath));
	}

	if (ADroneKeyPointDatasetActor* Actor = GetKeyPointActor())
	{
		KPointDetectionBtnTextBox->SetText(FText::FromString(Actor->OutputJsonPath));
	}

	if (ASceneObjectDatasetActor* Actor = GetSceneObjectActor())
	{
		SceneObjectExportPathTextBox->SetText(FText::FromString(Actor->OutputJsonPath));
	}
}

void USyntheticDataSectionWidget::OnRunSphericalContourClicked()
{
	if (ADroneDatasetGeneratorActor* Actor = GetDatasetActor())
	{
		Actor->GenerateDataset();
	}
}

void USyntheticDataSectionWidget::OnRunKPointDetectionClicked()
{
	if (ADroneKeyPointDatasetActor* Actor = GetKeyPointActor())
	{
		Actor->ExportKeyPoints();
	}
}

void USyntheticDataSectionWidget::OnRunSceneObjectExportClicked()
{
	if (ASceneObjectDatasetActor* Actor = GetSceneObjectActor())
	{
		Actor->ExportSceneObjects();
	}
}

void USyntheticDataSectionWidget::OnSphericalContourPathCommitted(const FText& Text, ETextCommit::Type /*CommitType*/)
{
	ADroneDatasetGeneratorActor* Actor = GetDatasetActor();
	if (!Actor) return;

	const FString BasePath = Text.ToString();
	Actor->OutputJsonPath = FPaths::Combine(BasePath, TEXT("template.json"));
	Actor->OutputImageDir = FPaths::Combine(BasePath, TEXT("images"));
}

void USyntheticDataSectionWidget::OnKPointDetectionPathCommitted(const FText& Text, ETextCommit::Type /*CommitType*/)
{
	if (ADroneKeyPointDatasetActor* Actor = GetKeyPointActor())
	{
		Actor->OutputJsonPath = Text.ToString();
	}
}

void USyntheticDataSectionWidget::OnSceneObjectExportPathCommitted(const FText& Text, ETextCommit::Type /*CommitType*/)
{
	if (ASceneObjectDatasetActor* Actor = GetSceneObjectActor())
	{
		Actor->OutputJsonPath = Text.ToString();
	}
}

ADroneDatasetGeneratorActor* USyntheticDataSectionWidget::GetDatasetActor() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<ADroneDatasetGeneratorActor>(
			UGameplayStatics::GetActorOfClass(World, ADroneDatasetGeneratorActor::StaticClass()));
	}
	return nullptr;
}

ADroneKeyPointDatasetActor* USyntheticDataSectionWidget::GetKeyPointActor() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<ADroneKeyPointDatasetActor>(
			UGameplayStatics::GetActorOfClass(World, ADroneKeyPointDatasetActor::StaticClass()));
	}
	return nullptr;
}

ASceneObjectDatasetActor* USyntheticDataSectionWidget::GetSceneObjectActor() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<ASceneObjectDatasetActor>(UGameplayStatics::GetActorOfClass(World, ASceneObjectDatasetActor::StaticClass()));
	}
	return nullptr;
}

AUAVSimulatorGameModeBase* USyntheticDataSectionWidget::GetGameMode() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<AUAVSimulatorGameModeBase>(World->GetAuthGameMode());
	}
	return nullptr;
}


void USyntheticDataSectionWidget::OnSegmentationMaskChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorSegmentationMask = bIsChecked;
}


void USyntheticDataSectionWidget::OnBBoxDetectionChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorBBoxDetection = bIsChecked;
}