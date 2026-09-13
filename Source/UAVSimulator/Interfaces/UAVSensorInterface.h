#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "UAVSensorInterface.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class UUAVSensorInterface : public UInterface { GENERATED_BODY() };

class UAVSIMULATOR_API IUAVSensorInterface
{
	GENERATED_BODY()
public:
	/** Керується RefreshSensorSettings; за замовчуванням false — сенсор інертний, доки його явно не ввімкнуть. */
	bool bSensorEnabled = false;

	/** Повертає назву топіка сенсора (напр. "camera", "lidar"). */
	virtual FString GetSensorTopic() const = 0;

	/**
	 * Заповнює OutFrame останніми підготованими даними й повертає true.
	 * Повертає false, якщо сенсор ще не виробив жодних даних.
	 * Викликається в ігровому потоці компонентом SensorBusComponent на кожному тіку шини.
	 */
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) = 0;
};
