#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "GeoPositionDroneComponent.generated.h"

class ACesiumGeoreference;

/**
 * Same role as UDronePositionComponent, but publishes the aircraft's position as
 * Latitude / Longitude / Altitude instead of a raw Unreal-space (X, Y, Z) position.
 *
 * Resolves ACesiumGeoreference::GetDefaultGeoreference in BeginPlay, then each tick converts
 * the owner's world-space location to the georeference's local reference frame
 * (InverseTransformPosition) before calling
 * ACesiumGeoreference::TransformUnrealPositionToLongitudeLatitudeHeight — the exact inverse of
 * the lat/long/height -> Unreal conversion used by UCustomSurroundingsScannerComponent::LoadObjects.
 *
 * Implements IUAVSensorInterface — SensorBusComponent auto-discovers this component and calls
 * GetLatestFrame() each bus tick.
 *
 * Payload format: {"latitude": <double>, "longitude": <double>, "altitude_m": <double>}
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UGeoPositionDroneComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UGeoPositionDroneComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("drone_geo_position"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

	/** Current geographic position (Longitude=X, Latitude=Y, Altitude in metres=Z), updated every tick. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geo Position")
	FVector LatestLongitudeLatitudeHeight = FVector::ZeroVector;

protected:
	virtual void BeginPlay() override;

private:
	/** Resolved in BeginPlay via ACesiumGeoreference::GetDefaultGeoreference. */
	UPROPERTY()
	ACesiumGeoreference* Georeference = nullptr;

	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
