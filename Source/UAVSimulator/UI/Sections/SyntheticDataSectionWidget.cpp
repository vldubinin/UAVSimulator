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
#include "UAVSimulator/Save/SyntheticDataSettingsSave.h"

const FString USyntheticDataSectionWidget::SyntheticDataSaveSlotName = TEXT("SyntheticDataSettings");

void USyntheticDataSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	LoadAndApplySavedSettings();
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
	SaveCurrentSettings();
}

void USyntheticDataSectionWidget::OnKPointDetectionPathCommitted(const FText& Text, ETextCommit::Type /*CommitType*/)
{
	if (ADroneKeyPointDatasetActor* Actor = GetKeyPointActor())
	{
		Actor->OutputJsonPath = Text.ToString();
	}
	SaveCurrentSettings();
}

void USyntheticDataSectionWidget::OnSceneObjectExportPathCommitted(const FText& Text, ETextCommit::Type /*CommitType*/)
{
	if (ASceneObjectDatasetActor* Actor = GetSceneObjectActor())
	{
		Actor->OutputJsonPath = Text.ToString();
	}
	SaveCurrentSettings();
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
	SaveCurrentSettings();
}


void USyntheticDataSectionWidget::OnBBoxDetectionChanged(bool bIsChecked)
{
	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
		GM->bEnableSensorBBoxDetection = bIsChecked;
	SaveCurrentSettings();
}

void USyntheticDataSectionWidget::LoadAndApplySavedSettings()
{
	USyntheticDataSettingsSave* Save = Cast<USyntheticDataSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(SyntheticDataSaveSlotName, /*UserIndex=*/0));
	if (!Save)
		return;

	if (ADroneDatasetGeneratorActor* Actor = GetDatasetActor())
	{
		if (!Save->SphericalContourBasePath.IsEmpty())
		{
			Actor->OutputJsonPath = FPaths::Combine(Save->SphericalContourBasePath, TEXT("template.json"));
			Actor->OutputImageDir = FPaths::Combine(Save->SphericalContourBasePath, TEXT("images"));
		}
	}

	if (ADroneKeyPointDatasetActor* Actor = GetKeyPointActor())
	{
		if (!Save->KeyPointOutputJsonPath.IsEmpty())
			Actor->OutputJsonPath = Save->KeyPointOutputJsonPath;
	}

	if (ASceneObjectDatasetActor* Actor = GetSceneObjectActor())
	{
		if (!Save->SceneObjectOutputJsonPath.IsEmpty())
			Actor->OutputJsonPath = Save->SceneObjectOutputJsonPath;
	}

	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		GM->bEnableSensorSegmentationMask = Save->bEnableSensorSegmentationMask;
		GM->bEnableSensorBBoxDetection    = Save->bEnableSensorBBoxDetection;
	}
}

void USyntheticDataSectionWidget::SaveCurrentSettings()
{
	USyntheticDataSettingsSave* Save = Cast<USyntheticDataSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(USyntheticDataSettingsSave::StaticClass()));

	if (ADroneDatasetGeneratorActor* Actor = GetDatasetActor())
	{
		Save->SphericalContourBasePath = FPaths::GetPath(Actor->OutputJsonPath);
	}

	if (ADroneKeyPointDatasetActor* Actor = GetKeyPointActor())
	{
		Save->KeyPointOutputJsonPath = Actor->OutputJsonPath;
	}

	if (ASceneObjectDatasetActor* Actor = GetSceneObjectActor())
	{
		Save->SceneObjectOutputJsonPath = Actor->OutputJsonPath;
	}

	if (AUAVSimulatorGameModeBase* GM = GetGameMode())
	{
		Save->bEnableSensorSegmentationMask = GM->bEnableSensorSegmentationMask;
		Save->bEnableSensorBBoxDetection    = GM->bEnableSensorBBoxDetection;
	}

	UGameplayStatics::SaveGameToSlot(Save, SyntheticDataSaveSlotName, /*UserIndex=*/0);
}