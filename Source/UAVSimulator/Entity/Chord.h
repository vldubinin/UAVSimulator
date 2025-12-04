#pragma once

#include "Runtime/Core/Public/Math/Vector.h"

struct Chord
{
	FVector StartPoint;
    FVector EndPoint;
    float Length;

    Chord()
        : StartPoint(FVector::ZeroVector)
        , EndPoint(FVector::ZeroVector)
        , Length(0.f)
    {}

    Chord(const FVector InStartPoint, const FVector InEndPoint)
        : StartPoint(InStartPoint)
        , EndPoint(InEndPoint)
        , Length(InEndPoint.X - InStartPoint.X)
    {}
};