#pragma once

#include "CoreMinimal.h"
#include "SensorFrame.generated.h"

USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FSensorFrame
{
	GENERATED_BODY()

	/** Ідентифікує сенсор: "camera", "imu", "gps" тощо. Використовується як топік ZMQ. */
	UPROPERTY(BlueprintReadOnly)
	FString Topic;

	/** Готовий до відправки серіалізований payload (байти JPEG для камери, упаковані float для решти). */
	UPROPERTY()
	TArray<uint8> Payload;

	/** Час світу в секундах, коли кадр було сформовано. */
	UPROPERTY(BlueprintReadOnly)
	double Timestamp = 0.0;
};
