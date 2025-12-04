#pragma once

#include "Runtime/Core/Public/Math/Vector.h"

struct AerodynamicForce
{
    FVector PositionalForce;
    FVector RotationalForce;

    AerodynamicForce()
        : PositionalForce(FVector::ZeroVector)
        , RotationalForce(FVector::ZeroVector)
    {}

    AerodynamicForce(const FVector LiftForce, const FVector DragForce, const FVector TorqueForce, FVector RelativePosition)
        : PositionalForce(LiftForce + DragForce)
        , RotationalForce(TorqueForce + FVector::CrossProduct(RelativePosition, LiftForce + DragForce))
    {}

};