// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/ProfileDataAsset/AerodynamicProfileDataAsset.h"
#include "UAVSimulator/Structure/AerodynamicProfileStructure.h"
#include "UAVSimulator/Structure/AerodynamicSurfaceStructure.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "UAVSimulator/Entity/SubSurface.h"
#include "UAVSimulator/SceneComponent/SubAerodynamicSurface/SubAerodynamicSurfaceSC.h"
#include "AerodynamicSurfaceSC.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UAVSIMULATOR_API UAerodynamicSurfaceSC : public USceneComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UAerodynamicSurfaceSC();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	void OnConstruction();

private:
	TArray<FAerodynamicProfileStructure> GetPoints();
	void BuildSubsurfaces(int32 Direction);

private:
	TArray<USubAerodynamicSurfaceSC*> SubSurfaces;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Аеродинамічний профіль"))
		UAerodynamicProfileDataAsset* AerodynamicProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Форма поверхні"))
		TArray<FAerodynamicSurfaceStructure> SurfaceForm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Зміщення аеродинамічного центру (%)"))
		float AerodynamicCenterOffsetPercent = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = "Симетрична поверхня"))
		bool Mirror = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration", meta = (DisplayName = ""))
		bool Enable = true;
};
