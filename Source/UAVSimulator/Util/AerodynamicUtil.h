// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Runtime/Core/Public/Math/Vector.h"
#include "Runtime/Core/Public/Math/Vector2D.h"
#include "UAVSimulator/Entity/Chord.h"
#include "AirfoilData.h"

/**
 * 
 */
class AerodynamicUtil
{
public:
	static Chord FindChord(TArray<FAirfoilPointData> Points, FVector Offset = FVector(0.f, 0.f, 0.f));
	static Chord FindChord(TArray<FVector> Points);
	static TArray<FAirfoilPointData> Scale(TArray<FAirfoilPointData> Points, float ScaleFactor);
	static TArray<FAirfoilPointData> NormalizePoints(TArray<FAirfoilPointData> Points);
	static TArray<FVector> ConvertTo3DPoints(TArray<FAirfoilPointData> Profile, float ChordLength, float ExpectedChordLength, FVector Offset);
	static TArray<FVector> AdaptTo(TArray<FAirfoilPointData> Points, FVector Offset);
	static TArray<FVector> ConvertToWorldCoordinates(USceneComponent* Component, TArray<FVector> LocalCoordinates);
	static FVector ConvertToWorldCoordinate(USceneComponent* Component, FVector LocalCoordinate);
	static Chord ConvertChordToWorldCoordinate(USceneComponent* Component, Chord LocalChord);
};