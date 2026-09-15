// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UAVSimulator/Entity/SimulatorMode.h"
#include "UAVSimulator/Entity/OnboardTargetMode.h"
#include "UAVSimulationSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnVisualSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnCameraSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnSensorSettingsChanged);
DECLARE_MULTICAST_DELEGATE(FOnEWSettingsChanged);

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

	/** Світові координати (X, Y, см) центрів усіх зон дії РЕБ на сцені. Перешкоди активні
	 *  автоматично для будь-якого літака в радіусі дії хоча б однієї зони — окремого
	 *  глобального вмикача немає. */
	TArray<FVector2D> EWLocations;

	/** Радіуси дії РЕБ (см), паралельно до EWLocations. */
	TArray<float> EWRadii;

	FOnVisualSettingsChanged OnVisualSettingsChanged;
	FOnCameraSettingsChanged OnCameraSettingsChanged;
	FOnSensorSettingsChanged OnSensorSettingsChanged;
	FOnEWSettingsChanged     OnEWSettingsChanged;

	void SetVisualSettings(bool bInPlayer, bool bInTarget);
	void SetOnboardCameraMode(EOnboardTargetMode Mode);
	void SetSensorSettings(EOnboardTargetMode InSensorsMode, bool bAltimeter, bool bAttitudeIndicator, bool bCameraInclination, bool bLidar, bool bCameraFrame, bool bCameraAltitude, bool bSegmentationMask, bool bBBoxDetection, bool bPosition, bool bGeoPosition, bool bCesiumSurroundings, bool bCustomSurroundings);
	void SetEWSettings(const TArray<FVector2D>& Locations, const TArray<float>& Radii);
};
