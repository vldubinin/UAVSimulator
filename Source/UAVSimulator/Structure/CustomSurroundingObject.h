#pragma once

#include "CoreMinimal.h"
#include "CustomSurroundingObject.generated.h"

/**
 * Один об'єкт, завантажений UCustomSurroundingsScannerComponent з його JSON-джерела: стабільний id,
 * довільний тег типу (напр. "building", "tree"), значення "altitude" і "bbox" — чотири
 * географічні кути ("x_min", "x_max", "y_min", "y_max", кожен — пара {latitude, longitude}),
 * що окреслюють контур об'єкта. Latitude/Longitude тут — середнє цих чотирьох
 * кутів (центр контуру); WorldLocationMeters — цей центр, конвертований у світові координати,
 * а BBoxCornersWorldMeters містить чотири кути, конвертовані так само, в порядку обходу
 * x_min -> x_max -> y_min -> y_max. Перепарситься з ObjectsJson щоразу, коли цей рядок змінюється
 * (див. LoadObjects()), тож кожне поле тут відображає живі зміни вихідного JSON без перезапуску.
 *
 * "altitude" з JSON НЕ використовується для розміщення маркерів: їхня висота береться з
 * вертикального трасування проти тайлів Cesium (див. UCustomSurroundingsScannerComponent::ResolveGroundHeights),
 * тож кожен кут і центр лежать точно на поверхні тайла. bGroundHeightResolved відстежує,
 * чи це прив'язування вже завершилося.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FCustomSurroundingObject
{
	GENERATED_BODY()

	/** "elementId" з вихідного JSON — стабільний ідентифікатор, використовується як ключ ObjectStorage. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	FString ObjectID;

	/** "type" з вихідного JSON (напр. "building", "tree"). */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	FString ObjectType;

	/** Середня широта чотирьох кутів "bbox", у градусах — центр контуру. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double Latitude = 0.0;

	/** Середня довгота чотирьох кутів "bbox", у градусах — центр контуру. */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double Longitude = 0.0;

	/**
	 * "altitude" з вихідного JSON, у метрах над еліпсоїдом. Лише інформативне — воно
	 * все ще передається в payload сенсора, але НЕ використовується для позиціонування маркерів
	 * (натомість їхня висота прив'язується до поверхні тайла Cesium, див. bGroundHeightResolved).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	double AltitudeMeters = 0.0;

	/**
	 * Центр контуру у світових координатах, у метрах — середнє BBoxCornersWorldMeters (кожен з
	 * яких — кут "bbox", пропущений через
	 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal під час завантаження).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	FVector WorldLocationMeters = FVector::ZeroVector;

	/**
	 * Чотири кути "bbox" у світових координатах, у метрах, у порядку обходу
	 * x_min -> x_max -> y_min -> y_max — кожен конвертований через
	 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal під час (пере)завантаження
	 * JSON, а потім прив'язаний по Z до поверхні тайла Cesium точно під/над ним
	 * (ResolveGroundHeights) — "altitude" з JSON не використовується. Керує відладковим контуром
	 * bbox та його проєкційним розміром у пікселях.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	TArray<FVector> BBoxCornersWorldMeters;

	/** Відстань від сканувального актора до WorldLocationMeters, у метрах — оновлюється на кожному Scan(). */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	float DistanceMeters = 0.0f;

	/**
	 * True, щойно кожен кут BBoxCornersWorldMeters (а через їхнє середнє — і WorldLocationMeters)
	 * прив'язано до поверхні тайла Cesium через
	 * UCustomSurroundingsScannerComponent::ResolveGroundHeights. До того прив'язування повторюється
	 * на кожному Scan() — Cesium підвантажує тайли залежно від відстані до камери, тож тайли
	 * далекого об'єкта можуть ще не існувати на момент завантаження. Скидається у false щоразу,
	 * коли вихідний JSON перезавантажується.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Custom Surroundings")
	bool bGroundHeightResolved = false;
};
