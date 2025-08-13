// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/AerodynamicProfileDataAsset.h"
#include "UAVSimulator/Structure/AerodynamicProfileStructure.h"
#include "UAVSimulator/Structure/AerodynamicSurfaceStructure.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "UAVSimulator/Entity/SubSurface.h"

#include "AerodynamicSurfaceActor.generated.h"



UCLASS()
class UAVSIMULATOR_API AAerodynamicSurfaceActor : public AActor
{
	GENERATED_BODY()
	
public:
	AAerodynamicSurfaceActor();

	virtual void OnConstruction(const FTransform& Transform) override;


private:
	TArray<FAerodynamicProfileStructure> NormalizePoints(TArray<FAerodynamicProfileStructure> Points);
	TArray<FAerodynamicProfileStructure> GetPoints();
	TArray<SubSurface> BuildSubsurfaces(int32 Direction);
	TArray<FVector> ConvertTo3DPoints(TArray<FAerodynamicProfileStructure> Profile, float ChordLength, float ExpectedChordLength, FVector Offset);
	void DrawSurface(TArray<SubSurface> Surface, FName SplineName);
	void DrawSpline(TArray<FVector> Points, FName SplineName);
	void DrawProfile(TArray<FAerodynamicProfileStructure> Points, FVector Offset, FName SplineName);
	void DrawChordConnections(TArray<Chord> Chords, FName SplineName);
	TArray<FVector> AdaptTo(TArray<FAerodynamicProfileStructure> Points, FVector Offset);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
		UAerodynamicProfileDataAsset* AerodynamicProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
		TArray<FAerodynamicSurfaceStructure> SurfaceForm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
		bool Mirror = false;
};
