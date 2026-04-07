// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "UAVSimulator/SceneComponent/ControlSurface/ControlSurfaceSC.h"
#include "UAVSimulator/Util/AerodynamicPhysicalCalculationUtil.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
#include "UAVSimulator/Entity/ControlInputState.h"
#include "UAVSimulator/Components/UAVCameraComponent.h"
#include "UAVSimulator/Components/UAVPhysicsStateComponent.h"

#include "PhysicalAirplane.generated.h"

UCLASS()
class UAVSIMULATOR_API APhysicalAirplane : public APawn
{
	GENERATED_BODY()

public:
	APhysicalAirplane();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, CallInEditor, meta = (DisplayName = "Розрахувати поляри для ЛА"), Category = "Автоматизація")
	void GenerateAerodynamicPhysicalConfigutation();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Налаштування симуляції",
		meta = (ToolTip = "Значення '1' відповідає звичайній швидкості.", DisplayName = "Швидкість роботи симуляції"))
	float DebugSimulatorSpeed = 1.0f;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateAileronControl"),  Category = "Control")
	void UpdateAileronControl(float LeftAileronAngle, float RightAileronAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateElevatorControl"), Category = "Control")
	void UpdateElevatorControl(float LeftElevatorAngle, float RightElevatorAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateRudderControl"),   Category = "Control")
	void UpdateRudderControl(float RudderAngle);

	/** Returns the processed camera output texture. Use this instead of the old Output Texture property. */
	UFUNCTION(BlueprintPure, Category = "Computer Vision")
	UTexture2D* GetCameraOutputTexture() const;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision", meta = (AllowPrivateAccess = "true"))
	UUAVCameraComponent* CameraComp;

	UPROPERTY()
	UUAVPhysicsStateComponent* PhysicsState;

	TArray<UAerodynamicSurfaceSC*>  Surfaces;
	TArray<UControlSurfaceSC*>      ControlSurfaces;

	float ThrottlePercent = 1.f;
	FControlInputState ControlState;
};
