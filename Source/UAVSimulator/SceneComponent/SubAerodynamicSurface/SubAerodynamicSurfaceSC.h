// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Entity/Chord.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileRow.h"
#include "Engine/DataTable.h"
#include "UAVSimulator/Entity/AerodynamicForce.h"
#include "UAVSimulator/Entity/ControlInputState.h"
#include "UAVSimulator/Entity/FlapType.h"
#include "UAVSimulator/SceneComponent/ControlSurface/ControlSurfaceSC.h"

#include "SubAerodynamicSurfaceSC.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UAVSIMULATOR_API USubAerodynamicSurfaceSC : public USceneComponent
{
	GENERATED_BODY()

public:
	USubAerodynamicSurfaceSC();

	/**
	 * Ініціалізує підповерхню: зберігає профілі, хорди, площу, центр тиску та відстань до центру мас.
	 * Також відображає відлагоджувальну візуалізацію поверхні, закрилка та міток у редакторі.
	 * @param InStart3DProfile        — 3D-точки профілю на початку розмаху.
	 * @param InEnd3DProfile          — 3D-точки профілю на кінці розмаху.
	 * @param SurfaceName             — ім'я секції для іменування відлагоджувальних об'єктів.
	 * @param CenterOfPressureOffset  — зміщення центру тиску вздовж хорди у відсотках (0–100).
	 * @param GlobalSurfaceCenterOfMass — центр мас ЛА у світових координатах.
	 * @param InMinFlapAngle          — мінімальний кут відхилення закрилка в градусах.
	 * @param InMaxFlapAngle          — максимальний кут відхилення закрилка в градусах.
	 * @param InStartFlapPosition     — початкова позиція шарніра закрилка на хорді (у відсотках).
	 * @param InEndFlapPosition       — кінцева позиція шарніра закрилка на хорді (у відсотках).
	 * @param ProfileAerodynamicTable — DataTable з полярними кривими; може бути nullptr (сили = 0).
	 * @param IsMirrorSurface         — true якщо це дзеркальна копія поверхні.
	 * @param SurfaceFlapType         — тип органу керування (елерон, руль висоти, руль напрямку).
	 * @param ControlSurfaceSC        — компонент, що фізично обертає закрилок у сцені; може бути nullptr.
	 */
	void InitComponent(TArray<FVector> InStart3DProfile, TArray<FVector> InEnd3DProfile,
		FName SurfaceName, float CenterOfPressureOffset, FVector GlobalSurfaceCenterOfMass,
		float InMinFlapAngle, float InMaxFlapAngle,
		float InStartFlapPosition, float InEndFlapPosition,
		UDataTable* ProfileAerodynamicTable, bool IsMirrorSurface,
		EFlapType SurfaceFlapType, UControlSurfaceSC* ControlSurfaceSC);

	/**
	 * Обчислює аеродинамічні сили та момент для цієї підповерхні за поточний кадр.
	 * Визначає кут атаки, знаходить полярний профіль та розраховує підйомну силу, опір і тангажний момент.
	 * @param LinearVelocity           — лінійна швидкість mesh у cm/s.
	 * @param AngularVelocity          — кутова швидкість mesh у рад/с.
	 * @param GlobalCenterOfMassInWorld — центр мас ЛА у світових координатах.
	 * @param AirflowDirection          — нормалізований вектор набігаючого потоку.
	 * @param ControlState             — поточний стан органів керування.
	 * @return FAerodynamicForce з позиційною силою (підйом+опір) та моментом.
	 */
	FAerodynamicForce CalculateForcesOnSubSurface(FVector LinearVelocity, FVector AngularVelocity,
		FVector GlobalCenterOfMassInWorld, FVector AirflowDirection, FControlInputState ControlState);

private:
	TArray<FVector> Start3DProfile;
	TArray<FVector> End3DProfile;
	FChord StartChord;
	FChord EndChord;
	float MinFlapAngle;
	float MaxFlapAngle;
	float StartFlapPosition;
	float EndFlapPosition;

	UDataTable* AerodynamicTable;

	static constexpr float AirDensity = 1.225f;
	const FVector Wind = FVector();
	bool IsMirror = false;
	EFlapType FlapType;
	UControlSurfaceSC* ControlSurface;

	FVector CenterOfPressure;
	float SurfaceArea;
	float DistanceToCenterOfMass;
};
