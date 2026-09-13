#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/SceneComponent/KeyPoint/KeyPointComponent.h"

#include "DroneKeyPointDatasetActor.generated.h"

/**
 * Інструментарний actor для редактора. Розмістіть на будь-якому рівні, призначте DroneBlueprintClass
 * (на ньому в Blueprint мають бути розміщені екземпляри UKeyPointComponent), задайте OutputJsonPath,
 * потім натисніть "Export Key Points".
 *
 * Спавнить дрон, зчитує позицію кожного UKeyPointComponent у локальній системі
 * координат дрона та експортує безрозмірні (нормалізовані) 3D-координати.
 *
 * Нормалізація: усі локальні позиції діляться на максимальне за модулем значення
 * координати серед усіх ключових точок, тож кожна компонента потрапляє в [-1, 1].
 * Сирий масштабний коефіцієнт (у см) зберігається в JSON, щоб позиції можна було відновити.
 *
 * Вихідний JSON:
 * {
 *   "drone_model": "MyDrone_C",
 *   "scale_cm": 245.3,
 *   "keypoints": [
 *     { "id": "nose",       "x":  0.501, "y":  0.000, "z":  0.120 },
 *     { "id": "left_wing",  "x": -0.980, "y": -0.100, "z":  0.050 }
 *   ]
 * }
 */
UCLASS(Blueprintable)
class UAVSIMULATOR_API ADroneKeyPointDatasetActor : public AActor
{
	GENERATED_BODY()

public:
	ADroneKeyPointDatasetActor();

	/** Blueprint дрона для семплювання. На ньому мають бути розміщені дочірні UKeyPointComponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	TSubclassOf<AActor> DroneBlueprintClass;

	/** Абсолютний шлях до вихідного JSON-файлу, наприклад C:/Datasets/drone_keypoints.json */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	FString OutputJsonPath;

	/** Натисніть, щоб заспавнити дрон, зібрати ключові точки та записати JSON-файл. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void ExportKeyPoints();

private:
	FString BuildJson(const FString& ModelName, float ScaleCm,
	                  const TArray<UKeyPointComponent*>& KPComps,
	                  const FTransform& DroneTransform) const;
};
