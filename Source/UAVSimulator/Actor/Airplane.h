// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "UAVSimulator/Components/AttitudeControlComponent.h"

#include "Airplane.generated.h"

class UUserWidget;

UCLASS()
class UAVSIMULATOR_API AAirplane : public APawn
{
	GENERATED_BODY()

public:
	AAirplane();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;

public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Computer Vision")
	UTexture2D* GetCameraOutputTexture() const;

	void RefreshConfigurations();
	void RefreshSensorSettings();
	void CleanupWidgets();

	/** Widget class to instantiate when the camera is active for this airplane. Set in Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Computer Vision")
	TSubclassOf<UUserWidget> CameraWidgetClass;

	/** Widget class to instantiate on the locally controlled pawn's HUD (e.g. WBP_AirplaneTelemetry). Set in Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Telemetry")
	TSubclassOf<UUserWidget> TelemetryWidgetClass;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision", meta = (AllowPrivateAccess = "true"))
	UUAVCameraComponent* CameraComp = nullptr;

	UPROPERTY()
	UUserWidget* CameraWidget = nullptr;

	UPROPERTY()
	UUserWidget* TelemetryWidget = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight", meta = (AllowPrivateAccess = "true"))
	UFlightDynamicsComponent* FlightDynamics;

	/**
	 * Присутній на кожному літаку (як FlightDynamics), але неактивний, доки хтось явно не
	 * викличе ActivateAutopilot() (напр. GameMode для Tracker-літака в PlaybackAndAutoTrack).
	 * Кожен Blueprint-нащадок AAirplane отримує власну, збережувану конфігурацію
	 * RollPid/PitchPid/YawRatePid у цій самій Details-панелі.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Autopilot", meta = (AllowPrivateAccess = "true"))
	UAttitudeControlComponent* AttitudeControl;
};
