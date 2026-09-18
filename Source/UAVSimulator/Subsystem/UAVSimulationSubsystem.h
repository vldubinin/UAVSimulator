// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UAVSimulator/Entity/SimulatorMode.h"
#include "UAVSimulator/Entity/OnboardTargetMode.h"
#include "UAVSimulationSubsystem.generated.h"

class AEWZoneActor;
class AWindActor;

DECLARE_MULTICAST_DELEGATE(FOnVisualSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnCameraSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnSensorSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnEWSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnWindSettingsChanged);

UCLASS()
class UAVSIMULATOR_API UUAVSimulationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	ESimulatorMode CurrentSimulatorMode = ESimulatorMode::RecordTarget;

	bool bEnableVisualsForPlayer = true;
	bool bEnableVisualsForTarget = false;

	/** Для якої ролі активна бортова камера (конвеєр USceneCaptureComponent2D). */
	EOnboardTargetMode OnboardCameraMode = EOnboardTargetMode::Drone;

	// — Експозиція бортової камери (щоб відповідати реальному сенсору-прототипу) ——————
	// За фізичною формулою (ISO + витримка + діафрагма), плюс фіксована EV-компенсація
	// зверху. bCameraManualExposure=false перемикає SceneCaptureComponent2D назад на
	// AEM_Histogram (авто-адаптація, як у головної камери гравця).
	bool  bCameraManualExposure = true;
	float CameraISO             = 100.0f;
	float CameraShutterSpeed    = 60.0f;
	float CameraApertureFStop   = 4.0f;
	float CameraExposureBias    = -3.4f;

	/** Для якої ролі активна шина сенсорів; окремі сенсори нижче додатково фільтруються за типом. */
	EOnboardTargetMode SensorsMode = EOnboardTargetMode::Drone;

	bool bEnableSensorAltimeter          = true;
	bool bEnableSensorAttitudeIndicator  = true;
	bool bEnableSensorCameraInclination  = true;
	bool bEnableSensorLidar              = true;
	bool bEnableSensorCameraFrame        = true;
	bool bEnableSensorCameraAltitude     = true;
	bool bEnableSensorSegmentationMask   = true;
	bool bEnableSensorBBoxDetection      = true;
	bool bEnableSensorPosition           = true;
	bool bEnableSensorGeoPosition        = true;
	bool bEnableSensorCesiumSurroundings = true;
	bool bEnableSensorCustomSurroundings = true;

	/** Усі AEWZoneActor на сцені. Кожна зона сама рахує свою інтенсивність перешкод
	 *  (AEWZoneActor::GetInterferenceIntensity) — тут лише список, без копій позиції/радіуса,
	 *  щоб ті не застарівали. TWeakObjectPtr — зони можуть бути переспавнені
	 *  (AEnvironmentActorManager::RefreshEWZones). Перешкоди активні автоматично для будь-якого
	 *  літака в радіусі дії хоча б однієї зони — окремого глобального вмикача немає. */
	TArray<TWeakObjectPtr<AEWZoneActor>> EWZones;

	/** Усі AWindActor на сцені. Кожен вектор сам рахує свій внесок у задану світову точку
	 *  (AWindActor::GetWindVelocityAtLocation) — тут лише список, без копій позиції/швидкості,
	 *  щоб ті не застарівали. TWeakObjectPtr — вектори можуть бути переспавнені
	 *  (AEnvironmentActorManager::RefreshWindVectors). */
	TArray<TWeakObjectPtr<AWindActor>> WindVectors;

	FOnVisualSettingsChanged OnVisualSettingsChanged;
	FOnCameraSettingsChanged OnCameraSettingsChanged;
	FOnSensorSettingsChanged OnSensorSettingsChanged;
	FOnEWSettingsChanged     OnEWSettingsChanged;
	FOnWindSettingsChanged   OnWindSettingsChanged;

	void SetVisualSettings(bool bInPlayer, bool bInTarget);
	void SetOnboardCameraMode(EOnboardTargetMode Mode);
	void SetCameraExposureSettings(bool bManualExposure, float ISO, float ShutterSpeed, float ApertureFStop, float ExposureBias);
	void SetSensorSettings(EOnboardTargetMode InSensorsMode, bool bAltimeter, bool bAttitudeIndicator, bool bCameraInclination, bool bLidar, bool bCameraFrame, bool bCameraAltitude, bool bSegmentationMask, bool bBBoxDetection, bool bPosition, bool bGeoPosition, bool bCesiumSurroundings, bool bCustomSurroundings);
	void SetEWSettings(const TArray<TWeakObjectPtr<AEWZoneActor>>& Zones);
	void SetWindSettings(const TArray<TWeakObjectPtr<AWindActor>>& Vectors);

	/** Сумарна швидкість вітру у заданій світовій точці, см/с — векторна сума внесків усіх
	 *  WindVectors (на відміну від EW-перешкод, які беруть максимум: вітер — фізична
	 *  швидкість, тож перекриваючі поля мають складатися, як і в наявній системі вихрового
	 *  сліду, UFlightDynamicsComponent::GetInducedVelocity, а не РЕБ-абстракція [0,1]). */
	FVector GetWindVelocityAtLocation(const FVector& WorldLocation) const;
};
