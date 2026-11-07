#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "FlightTruthComponent.generated.h"

class ACesiumGeoreference;
class UFlightDynamicsComponent;
class UAttitudeControlComponent;
class UStaticMeshComponent;

/**
 * "Ground truth" стану польоту для лабораторного стенда льотних випробувань
 * (Tools/TestingPlatform/flight_lab). Публікується через існуючий USensorBusComponent
 * під топіком "flight_truth" — окремого транспорту немає.
 *
 * На відміну від звичайних сенсорів (що моделюють бортове обладнання), це «погляд
 * інструктора»: точна позиція/швидкість/орієнтація, аеродинамічні кути, стан двигуна,
 * уставка + вихід внутрішнього PID автопілота, вік останньої команди та вплив оточення
 * (вітер, РЕБ). Стенд пише його в телеметрію для аналізу навіть тоді, коли алгоритм
 * користувача свідомо не має до нього доступу (use_truth: false).
 *
 * CDO на AAirplane. Вмикається в AAirplane::RefreshSensorSettings() за прапорцем
 * UUAVSimulationSubsystem::bEnableSensorFlightTruth.
 *
 * Світова система координат Unreal (лівостороння): X, Y — горизонт, Z — вгору; навігаційно
 * значущі координати — geo (lat/lon/alt). Кути — градуси, швидкості — м/с, позиції — м.
 * Формат payload — див. Docs/14-Flight-Test-Lab.md.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UFlightTruthComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UFlightTruthComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("flight_truth"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

private:
	void CaptureState(float DeltaTime);

	UPROPERTY()
	TObjectPtr<ACesiumGeoreference> Georeference = nullptr;

	UPROPERTY()
	TObjectPtr<UFlightDynamicsComponent> FlightDynamics = nullptr;

	UPROPERTY()
	TObjectPtr<UAttitudeControlComponent> AttitudeControl = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	/** Останній знімок стану, готовий до серіалізації. */
	TSharedPtr<FJsonObject> LatestState;
	double LatestTimestamp = 0.0;

	FVector PrevVelocityCmS = FVector::ZeroVector;
	bool    bHasPrevVelocity = false;
};
