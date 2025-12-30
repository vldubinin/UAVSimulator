// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Components/SplineComponent.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/Util/AerodynamicPhysicalCalculationUtil.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
#include "UAVSimulator/Entity/ControlInputState.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "PreOpenCVHeaders.h"
#include "OpenCVHelper.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/core.hpp>
#include "PostOpenCVHeaders.h"

#include "PhysicalAirplane.generated.h"

UCLASS()
class UAVSIMULATOR_API APhysicalAirplane : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	APhysicalAirplane();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void OnConstruction(const FTransform& Transform) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;


protected:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Generate Aerodynamic Physical Configutation"), Category = "Editor Tools")
		void GenerateAerodynamicPhysicalConfigutation(UObject* ContextObject);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft | Unreal", meta = (ToolTip = "Швидкість роботи симуляції. Значення '1' відповідає звичайній швидкості."))
		float DebugSimulatorSpeed = 1.0f;

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateAileronControl"), Category = "Control")
	void UpdateAileronControl(float LeftAileronAngle, float RightAileronAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateElevatorControl"), Category = "Control")
	void UpdateElevatorControl(float LeftElevatorAngle, float RightElevatorAngle);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "UpdateRudderControl"), Category = "Control")
	void UpdateRudderControl(float RudderAngle);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Computer Vision")
	USceneCaptureComponent2D* CVCaptureComponent;

	// Текстура для рендеру (створимо динамічно в C++)
	UPROPERTY()
	UTextureRenderTarget2D* CVRenderTarget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Computer Vision")
	UTexture2D* OutputTexture;

	// Регіон оновлення (технічна змінна для оптимізації)
	FUpdateTextureRegion2D* VideoUpdateTextureRegion;

	// Параметри камери
	const int32 CVWidth = 640;
	const int32 CVHeight = 480;

public:
	// Функція для отримання та обробки кадру (викликайте в Tick)
	void ProcessCameraFrame();

	void UpdateTexture();

	// Змінна для збереження обробленого зображення між кадрами (щоб не втратити дані до оновлення)
	cv::Mat ProcessedFrameBuffer;

private:
	void CalculateParameters();


private:
	TArray<UAerodynamicSurfaceSC*> Surfaces;
	TArray<UControlSurfaceSC*> ControlSurfaces;
	FVector LinearVelocity;
	FVector AngularVelocity;
	FVector CenterOfMassInWorld;
	FVector AirflowDirection;

	float ThrottlePercent = 1.f;

	ControlInputState ControlState;
};
