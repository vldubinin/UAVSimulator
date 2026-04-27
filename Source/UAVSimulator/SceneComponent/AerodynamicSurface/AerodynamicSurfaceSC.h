// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileRow.h"
#include "UAVSimulator/Structure/AerodynamicSurfaceStructure.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "UAVSimulator/SceneComponent/SubAerodynamicSurface/SubAerodynamicSurfaceSC.h"
#include "Runtime/Engine/Public/DrawDebugHelpers.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
#include "AirfoilData.h"
#include "UAVSimulator/Entity/ControlInputState.h"

#include "AerodynamicSurfaceSC.generated.h"




UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UAVSIMULATOR_API UAerodynamicSurfaceSC : public USceneComponent
{
	GENERATED_BODY()

public:
	/** Вмикає тік компонента. */
	UAerodynamicSurfaceSC();

public:
	/**
	 * Ініціалізує аеродинамічну поверхню: знищує старі підповерхні та будує нові на основі SurfaceForm.
	 * За потреби дзеркально дублює підповерхні (Mirror == true).
	 * @param CenterOfMass — центр мас ЛА у світових координатах для розрахунку моментів.
	 * @param ControlSur   — усі керуючі поверхні актора для прив'язки до підсекцій.
	 */
	void OnConstruction(FVector CenterOfMass, TArray<UControlSurfaceSC*> ControlSur);

	/**
	 * Підсумовує аеродинамічні сили та моменти від усіх підповерхонь.
	 * @param CenterOfMass    — центр мас ЛА у світових координатах.
	 * @param LinearVelocity  — лінійна швидкість mesh у cm/s.
	 * @param AngularVelocity — кутова швидкість mesh у рад/с.
	 * @param AirflowDirection — нормалізований вектор набігаючого потоку.
	 * @param ControlState    — поточний стан органів керування.
	 * @return Сумарна аеродинамічна сила та момент для всієї поверхні.
	 */
	AerodynamicForce CalculateForcesOnSurface(FVector CenterOfMass, FVector LinearVelocity, FVector AngularVelocity, FVector AirflowDirection, ControlInputState ControlState, bool bVisualizeForces);

	/** Activates or deactivates all UNiagaraComponents attached to this surface. */
	void SetNiagaraActive(bool bActive);

private:
	/**
	 * Читає усі рядки з DataTable Profile та повертає масив точок профілю крила.
	 * @return Масив FAirfoilPointData; порожній якщо Profile не задано.
	 */
	TArray<FAirfoilPointData> GetPoints();

	/**
	 * Будує підповерхні (USubAerodynamicSurfaceSC) на основі масиву SurfaceForm.
	 * @param CenterOfMass — центр мас ЛА для розрахунку відстані до центру тиску.
	 * @param Direction    — +1 для основної сторони, -1 для дзеркальної.
	 */
	void BuildSubsurfaces(FVector CenterOfMass, int32 Direction);

	/** Знищує всі раніше створені підповерхні та очищає масив SubSurfaces. */
	void DestroySubsurfaces();

	/**
	 * Шукає керуючу поверхню заданого типу з потрібним прапором дзеркальності.
	 * @param Type    — тип поверхні (Aileron, Elevator, Rudder).
	 * @param bMirror — true якщо потрібна дзеркальна поверхня.
	 * @return Вказівник на знайдену UControlSurfaceSC або nullptr.
	 */
	UControlSurfaceSC* FindControlSurface(EFlapType Type, bool bMirror);
	
private:
	TArray<USubAerodynamicSurfaceSC*> SubSurfaces;
	TArray<UControlSurfaceSC*> ControlSurfaces;

protected:


public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Аеродинамічний профіль"))
		UDataTable* Profile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Форма поверхні"))
		TArray<FAerodynamicSurfaceStructure> SurfaceForm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Зміщення аеродинамічного центру (%)"))
		float AerodynamicCenterOffsetPercent = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Симетрична поверхня"))
		bool Mirror = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = ""))
		bool Enable = true;
};
