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
	/** Створює субкомпоненти камери та фізичного стану. */
	APhysicalAirplane();

protected:
	/** Запускає симуляцію: встановлює дилатацію часу, ініціалізує поверхні з центром мас. */
	virtual void BeginPlay() override;

	/**
	 * Викликається при розміщенні актора в редакторі або зміні трансформу.
	 * Збирає всі аеродинамічні поверхні та керуючі поверхні, передає центр мас кожній поверхні.
	 */
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	/**
	 * Головний тік симуляції: обробляє кадр камери, оновлює фізичний стан,
	 * накопичує сили від усіх поверхонь і застосовує тягу двигуна до mesh-компонента.
	 * @param DeltaTime — час кадру в секундах.
	 */
	virtual void Tick(float DeltaTime) override;

	/**
	 * Запускає генерацію аеродинамічних полярних характеристик для всіх поверхонь ЛА
	 * через утиліту AerodynamicPhysicalCalculationUtil (OpenVSP/XFoil pipeline).
	 * Виконується прямо в редакторі (CallInEditor).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, meta = (DisplayName = "Розрахувати поляри для ЛА"), Category = "Автоматизація")
	void GenerateAerodynamicPhysicalConfigutation();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Налаштування симуляції",
		meta = (ToolTip = "Значення '1' відповідає звичайній швидкості.", DisplayName = "Швидкість роботи симуляції"))
	float DebugSimulatorSpeed = 1.0f;

	/**
	 * Оновлює кути відхилення елеронів у поточному стані керування.
	 * @param LeftAileronAngle  — кут лівого елерона в градусах.
	 * @param RightAileronAngle — кут правого елерона в градусах.
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateAileronControl"),  Category = "Control")
	void UpdateAileronControl(float LeftAileronAngle, float RightAileronAngle);

	/**
	 * Оновлює кути відхилення рулів висоти у поточному стані керування.
	 * @param LeftElevatorAngle  — кут лівого руля висоти в градусах.
	 * @param RightElevatorAngle — кут правого руля висоти в градусах.
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateElevatorControl"), Category = "Control")
	void UpdateElevatorControl(float LeftElevatorAngle, float RightElevatorAngle);

	/**
	 * Оновлює кут відхилення руля напрямку у поточному стані керування.
	 * @param RudderAngle — кут руля напрямку в градусах.
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateRudderControl"),   Category = "Control")
	void UpdateRudderControl(float RudderAngle);

	/**
	 * Повертає оброблену OpenCV текстуру з бортової камери.
	 * @return Вказівник на UTexture2D для прив'язки у віджеті або матеріалі; nullptr якщо камера не ініціалізована.
	 */
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
