#pragma once

#include "CoreMinimal.h"
#include "SensorFrame.generated.h"

USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FSensorFrame
{
	GENERATED_BODY()

	/** Identifies the sensor: "camera", "imu", "gps", etc. Used as ZMQ topic. */
	UPROPERTY(BlueprintReadOnly)
	FString Topic;

	/** Ready-to-send serialized payload (JPEG bytes for camera, packed floats for others). */
	UPROPERTY()
	TArray<uint8> Payload;

	/** World time in seconds when the frame was produced. */
	UPROPERTY(BlueprintReadOnly)
	double Timestamp = 0.0;
};
