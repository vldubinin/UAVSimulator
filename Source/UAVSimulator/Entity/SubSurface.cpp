#include "SubSurface.h"


SubSurface::SubSurface(const TArray<FVector>& InStart3DProfile, const TArray<FVector>& InEnd3DProfile)
    : Start3DProfile(InStart3DProfile),
    End3DProfile(InEnd3DProfile),
    StartChord(AerodynamicUtil::FindChord(InStart3DProfile)),
    EndChord(AerodynamicUtil::FindChord(InEnd3DProfile))
{

}

const TArray<FVector>& SubSurface::GetStart3DProfile() const {
    return Start3DProfile;
}

const TArray<FVector>& SubSurface::GetEnd3DProfile() const {
    return End3DProfile;
}

const Chord& SubSurface::GetStartChord() const {
    return StartChord;
}

const Chord& SubSurface::GetEndChord() const {
    return EndChord;
}