#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "GeoPositionDroneComponent.generated.h"

class ACesiumGeoreference;
class UFlightDynamicsComponent;

/**
 * Виконує ту саму роль, що й UDronePositionComponent, але публікує позицію літака як
 * широту / довготу / висоту замість «сирої» позиції (X, Y, Z) у просторі Unreal.
 *
 * У BeginPlay визначає ACesiumGeoreference::GetDefaultGeoreference, а потім щокадру конвертує
 * світову позицію власника у локальну систему відліку геореференсу (InverseTransformPosition)
 * перед викликом ACesiumGeoreference::TransformUnrealPositionToLongitudeLatitudeHeight — це точна
 * зворотна операція до конвертації lat/long/height -> Unreal, яку використовує
 * UCustomSurroundingsScannerComponent::LoadObjects.
 *
 * Реалізує IUAVSensorInterface — SensorBusComponent автоматично знаходить цей компонент і викликає
 * GetLatestFrame() на кожному такті шини.
 *
 * Формат payload: {"latitude": <double>, "longitude": <double>, "altitude_m": <double>}
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

	/** Поточна географічна позиція (Довгота=X, Широта=Y, Висота в метрах=Z), оновлюється щокадру. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geo Position")
	FVector LatestLongitudeLatitudeHeight = FVector::ZeroVector;

protected:
	virtual void BeginPlay() override;

private:
	/** Визначається в BeginPlay через ACesiumGeoreference::GetDefaultGeoreference. */
	UPROPERTY()
	ACesiumGeoreference* Georeference = nullptr;

	/** Визначається в BeginPlay через FindComponentByClass — використовується лише для діагностичного логу розмаху крила нижче. */
	UPROPERTY()
	UFlightDynamicsComponent* FlightDynamics = nullptr;

	double LatestTimestamp = 0.0;
	bool   bHasData        = false;
};
