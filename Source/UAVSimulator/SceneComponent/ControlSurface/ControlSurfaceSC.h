// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UAVSimulator/Entity/FlapType.h"

#include "ControlSurfaceSC.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UAVSIMULATOR_API UControlSurfaceSC : public USceneComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UControlSurfaceSC();

public:
	void Move(float Angle);

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Тип керуючої поверхні"))
		EFlapType FlapType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Симетрія"))
		bool IsMirror;
};
