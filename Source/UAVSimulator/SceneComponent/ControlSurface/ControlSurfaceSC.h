// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/AxisType.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Entity/FlapType.h"
#include "UAVSimulator/Entity/ActuatorDynamics.h"

#include "ControlSurfaceSC.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UAVSIMULATOR_API UControlSurfaceSC : public USceneComponent
{
	GENERATED_BODY()

public:
	/** Ініціалізує тік компонента (увімкнено). */
	UControlSurfaceSC();

public:
	/**
	 * Просуває привід поверхні на DeltaTime у бік TargetAngle (див. FActuatorDynamics) та
	 * обертає керуючу поверхню навколо налаштованої осі до фактичного (лагованого) кута.
	 * Якщо IsReverseDirection == true, знак кута інвертується лише для візуального обертання —
	 * внутрішній стан приводу та повернене значення лишаються в "логічних" градусах.
	 * @param TargetAngle — цільовий (командний) кут відхилення в градусах.
	 * @param DeltaTime   — час з попереднього виклику, с.
	 * @return Фактичний (лагований) кут приводу в градусах — використовується, зокрема, для
	 *         пошуку аеродинамічного профілю за реальним, а не командним положенням керма.
	 */
	float Move(float TargetAngle, float DeltaTime);

	/** @return Поточний фактичний кут приводу в градусах ("логічний", без урахування IsReverseDirection). */
	float GetCurrentAngle() const { return Actuator.Angle; }

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Тип керуючої поверхні"))
		EFlapType FlapType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Вісь обертання"))
		EAxisType AxisType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Симетрія"))
		bool IsMirror;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Зворотній напрямок"))
		bool IsReverseDirection;

	/** Динаміка приводу цієї поверхні (перехідний процес відхилення). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator", meta = (DisplayName = "Динаміка приводу"))
		FActuatorDynamics Actuator;

	/** Логувати в консоль бажаний (командний) і фактичний (лагований) кут цієї поверхні щотіку. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actuator", meta = (DisplayName = "Логувати кути (debug)"))
		bool bLogAngleDebug = false;
};
