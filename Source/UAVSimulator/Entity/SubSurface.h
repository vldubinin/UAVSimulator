#pragma once

#include "UAVSimulator/Entity/Chord.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "Runtime/Core/Public/Math/Vector.h"



class SubSurface
{
public:
    SubSurface(const TArray<FVector>& InStart3DProfile, const TArray<FVector>& InEnd3DProfile);

    const TArray<FVector>& GetStart3DProfile() const;
    const TArray<FVector>& GetEnd3DProfile() const;
    const Chord& GetStartChord() const;
    const Chord& GetEndChord() const;

private:
    TArray<FVector> Start3DProfile;
    TArray<FVector> End3DProfile;
    Chord StartChord;
    Chord EndChord;
};
