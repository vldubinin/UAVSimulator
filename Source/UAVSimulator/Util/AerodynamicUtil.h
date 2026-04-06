// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/Chord.h"
#include "AirfoilData.h"

/**
 * Stateless airfoil geometry utilities: chord finding, profile scaling, normalization, and 3D conversion.
 * Coordinate-space transforms live in CoordinateTransformUtil.
 */
class UAVSIMULATOR_API AerodynamicUtil
{
public:
	/** Find the chord (min-X to max-X) of a 2D airfoil point set, with an optional offset. */
	static FChord FindChord(TArray<FAirfoilPointData> Points, FVector Offset = FVector::ZeroVector);

	/** Find the chord of a 3D point array by X extents. */
	static FChord FindChord(TArray<FVector> Points);

	/** Scale a 2D airfoil profile around its chord midpoint. */
	static TArray<FAirfoilPointData> Scale(TArray<FAirfoilPointData> Points, float ScaleFactor);

	/** Flip the X axis of a 2D profile (coordinate convention normalisation). */
	static TArray<FAirfoilPointData> NormalizePoints(TArray<FAirfoilPointData> Points);

	/** Scale a 2D profile to ExpectedChordLength and lift it into 3D at the given offset. */
	static TArray<FVector> ConvertTo3DPoints(TArray<FAirfoilPointData> Profile, float ChordLength, float ExpectedChordLength, FVector Offset);

	/** Lift 2D airfoil points into 3D by setting Y = 0 and adding an offset. */
	static TArray<FVector> AdaptTo(TArray<FAirfoilPointData> Points, FVector Offset);
};
