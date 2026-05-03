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
	/** Returns the sensor's topic name (e.g. "camera", "lidar"). */
	virtual FString GetSensorTopic() const = 0;

	/**
	 * Fills OutFrame with the most recently prepared data and returns true.
	 * Returns false if the sensor has not produced any data yet.
	 * Called on the game thread by SensorBusComponent each bus tick.
	 */
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) = 0;
};
