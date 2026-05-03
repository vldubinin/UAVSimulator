#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "UAVSensorInterface.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSensorDataReady, const FSensorFrame&);

UINTERFACE(MinimalAPI, NotBlueprintable)
class UUAVSensorInterface : public UInterface { GENERATED_BODY() };

class UAVSIMULATOR_API IUAVSensorInterface
{
	GENERATED_BODY()
public:
	/** Returns the sensor's ZMQ topic name (e.g. "camera", "imu"). */
	virtual FString GetSensorTopic() const = 0;

	/** Returns the delegate that fires when a new encoded frame is ready. */
	virtual FOnSensorDataReady& GetOnSensorDataReady() = 0;
};
