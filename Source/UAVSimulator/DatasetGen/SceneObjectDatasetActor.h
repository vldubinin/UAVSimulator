#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SceneObjectDatasetActor.generated.h"

/**
 * Інструментарний actor для редактора/рантайму. Розмістіть на будь-якому рівні, задайте OutputJsonPath,
 * потім натисніть "Export Scene Objects" (або викличте ExportSceneObjects() з UI).
 *
 * Сканує кожен actor, наявний у світі, і експортує його позицію в світових координатах
 * та розмір axis-aligned bounding box (в одиницях Unreal, як і позиція).
 * Дрони гравця і цілі (екземпляри AAirplane) виключаються перевіркою класу —
 * теги чи інше маркування actor'ів не потрібні. Actor'и, чиє ім'я класу
 * збігається з елементом ExcludedActorClassNames, також пропускаються. Actor'и без
 * UStaticMeshComponent (game mode, контролери, volume, actor'и підсистем тощо)
 * пропускаються, оскільки вони не є фізичними об'єктами сцени.
 *
 * Вихідний JSON:
 * {
 *   "objects": [
 *     { "name": "Building_1", "class": "StaticMeshActor", "x": 120.0, "y": -50.0, "z": 0.0,
 *       "size_x": 200.0, "size_y": 150.0, "size_z": 300.0 }
 *   ]
 * }
 */
UCLASS(Blueprintable)
class UAVSIMULATOR_API ASceneObjectDatasetActor : public AActor
{
	GENERATED_BODY()

public:
	ASceneObjectDatasetActor();

	/** Абсолютний шлях до вихідного JSON-файлу, наприклад C:/Datasets/scene_objects.json */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	FString OutputJsonPath = TEXT("C:/Datasets/scene_objects.json");

	/** Імена класів actor'ів, які виключаються з експорту (звіряються з GetClass()->GetName(),
	 *  наприклад "StaticMeshActor"). Екземпляри AAirplane завжди виключаються незалежно від цього списку. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	TArray<FString> ExcludedActorClassNames;

	/** Натисніть, щоб просканувати сцену та записати JSON-файл. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void ExportSceneObjects();

private:
	bool IsExcludedClass(const AActor* Actor) const;
	FString BuildJson(const TArray<AActor*>& Objects) const;
};
