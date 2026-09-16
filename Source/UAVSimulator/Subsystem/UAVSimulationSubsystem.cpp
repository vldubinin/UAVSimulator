// Fill out your copyright notice in the Description page of Project Settings.

#include "UAVSimulationSubsystem.h"
#include "UAVSimulator/Actor/WindActor.h"

void UUAVSimulationSubsystem::SetVisualSettings(bool bInPlayer, bool bInTarget)
{
	bEnableVisualsForPlayer = bInPlayer;
	bEnableVisualsForTarget = bInTarget;
	OnVisualSettingsChanged.Broadcast();
}

void UUAVSimulationSubsystem::SetOnboardCameraMode(EOnboardTargetMode Mode)
{
	OnboardCameraMode = Mode;
	OnCameraSettingsChanged.Broadcast();
}

void UUAVSimulationSubsystem::SetSensorSettings(EOnboardTargetMode InSensorsMode, bool bAltimeter, bool bAttitudeIndicator, bool bCameraInclination, bool bLidar, bool bCameraFrame, bool bCameraAltitude, bool bSegmentationMask, bool bBBoxDetection, bool bPosition, bool bGeoPosition, bool bCesiumSurroundings, bool bCustomSurroundings)
{
	SensorsMode                    = InSensorsMode;
	bEnableSensorAltimeter         = bAltimeter;
	bEnableSensorAttitudeIndicator = bAttitudeIndicator;
	bEnableSensorCameraInclination = bCameraInclination;
	bEnableSensorLidar             = bLidar;
	bEnableSensorCameraFrame	   = bCameraFrame;
	bEnableSensorCameraAltitude    = bCameraAltitude;
	bEnableSensorSegmentationMask  = bSegmentationMask;
	bEnableSensorBBoxDetection     = bBBoxDetection;
	bEnableSensorPosition          = bPosition;
	bEnableSensorGeoPosition       = bGeoPosition;
	bEnableSensorCesiumSurroundings = bCesiumSurroundings;
	bEnableSensorCustomSurroundings = bCustomSurroundings;
	OnSensorSettingsChanged.Broadcast();
}

void UUAVSimulationSubsystem::SetEWSettings(const TArray<TWeakObjectPtr<AEWZoneActor>>& Zones)
{
	EWZones = Zones;
	OnEWSettingsChanged.Broadcast();
}

void UUAVSimulationSubsystem::SetWindSettings(const TArray<TWeakObjectPtr<AWindActor>>& Vectors)
{
	WindVectors = Vectors;
	OnWindSettingsChanged.Broadcast();
}

FVector UUAVSimulationSubsystem::GetWindVelocityAtLocation(const FVector& WorldLocation) const
{
	FVector TotalWind = FVector::ZeroVector;
	for (const TWeakObjectPtr<AWindActor>& WindPtr : WindVectors)
	{
		if (AWindActor* Wind = WindPtr.Get())
			TotalWind += Wind->GetWindVelocityAtLocation(WorldLocation);
	}
	return TotalWind;
}
