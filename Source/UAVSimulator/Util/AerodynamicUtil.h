// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Runtime/Core/Public/Math/Vector2D.h"
#include "UAVSimulator/Entity/Chord.h"
#include "UAVSimulator/Structure/AerodynamicProfileStructure.h"

/**
 * 
 */
class AerodynamicUtil
{
public:
	static Chord FindChord(TArray<FAerodynamicProfileStructure> Points, FVector Offset = FVector(0.f, 0.f, 0.f));
	static Chord FindChord(TArray<FVector> Points);
	static TArray<FAerodynamicProfileStructure> Scale(TArray<FAerodynamicProfileStructure> Points, float ScaleFactor);
	static TArray<FAerodynamicProfileStructure> NormalizePoints(TArray<FAerodynamicProfileStructure> Points);
	static TArray<FVector> ConvertTo3DPoints(TArray<FAerodynamicProfileStructure> Profile, float ChordLength, float ExpectedChordLength, FVector Offset);
	static TArray<FVector> AdaptTo(TArray<FAerodynamicProfileStructure> Points, FVector Offset);
	static TArray<FVector> ConvertToWorldCoordinates(USceneComponent* Component, TArray<FVector> LocalCoordinates);
	static FVector ConvertToWorldCoordinate(USceneComponent* Component, FVector LocalCoordinate);
};