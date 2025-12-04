// Fill out your copyright notice in the Description page of Project Settings.


#include "ControlSurfaceSC.h"

// Sets default values for this component's properties
UControlSurfaceSC::UControlSurfaceSC()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


void UControlSurfaceSC::Move(float Angle)
{
	UE_LOG(LogTemp, Warning, TEXT("Control Angel: %f"), Angle);


	FRotator NewRotation = FRotator(Angle, 0.f, 0.f);
	SetRelativeRotation(NewRotation);
}