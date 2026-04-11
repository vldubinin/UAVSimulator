// Fill out your copyright notice in the Description page of Project Settings.


#include "ControlSurfaceSC.h"
#include "UAVSimulator/UAVSimulator.h"

UControlSurfaceSC::UControlSurfaceSC()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UControlSurfaceSC::Move(float Angle)
{
	// Інвертуємо кут якщо поверхня позначена як зворотна (наприклад, симетричний елерон)
	Angle = IsReverseDirection ? Angle * -1 : Angle;
	UE_LOG(LogUAV, Warning, TEXT("%s Control Angle: %f"), *UEnum::GetValueAsString(FlapType), Angle);

	// Кожна вісь повертає компонент по-різному: X — крен (Roll), Y — тангаж (Pitch), Z — рискання (Yaw)
	FRotator NewRotation;
	if (AxisType == EAxisType::X) {
		NewRotation = FRotator(0.f, 0.f, Angle);  // Roll
	}
	else if (AxisType == EAxisType::Y) {
		NewRotation = FRotator(Angle, 0.f, 0.f);  // Pitch
	}
	else {
		NewRotation = FRotator(0.f, Angle, 0.f);  // Yaw
	}

	SetRelativeRotation(NewRotation);
}