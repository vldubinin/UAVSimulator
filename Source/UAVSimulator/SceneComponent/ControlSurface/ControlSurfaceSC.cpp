// Fill out your copyright notice in the Description page of Project Settings.


#include "ControlSurfaceSC.h"
#include "UAVSimulator/UAVSimulator.h"

UControlSurfaceSC::UControlSurfaceSC()
{
	PrimaryComponentTick.bCanEverTick = true;
}

float UControlSurfaceSC::Move(float TargetAngle, float DeltaTime)
{
	// Просуваємо привід у бік командного кута з урахуванням перехідного процесу (див. FActuatorDynamics).
	const float LogicalAngle = Actuator.Advance(TargetAngle, DeltaTime);

	// Інвертуємо кут якщо поверхня позначена як зворотна (наприклад, симетричний елерон) —
	// лише для візуального обертання; внутрішній стан приводу лишається "логічним".
	const float VisualAngle = IsReverseDirection ? LogicalAngle * -1 : LogicalAngle;

	if (bLogAngleDebug)
	{
		UE_LOG(LogUAV, Log, TEXT("%s [%s]: бажаний=%.2f° фактичний=%.2f°"),
			*GetName(), *UEnum::GetValueAsString(FlapType), TargetAngle, LogicalAngle);
	}

	// Кожна вісь повертає компонент по-різному: X — крен (Roll), Y — тангаж (Pitch), Z — рискання (Yaw)
	FRotator NewRotation;
	if (AxisType == EAxisType::X) {
		NewRotation = FRotator(0.f, 0.f, VisualAngle);  // Roll
	}
	else if (AxisType == EAxisType::Y) {
		NewRotation = FRotator(VisualAngle, 0.f, 0.f);  // Pitch
	}
	else {
		NewRotation = FRotator(0.f, VisualAngle, 0.f);  // Yaw
	}

	SetRelativeRotation(NewRotation);

	return LogicalAngle;
}