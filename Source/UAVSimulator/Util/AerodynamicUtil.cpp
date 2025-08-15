// Fill out your copyright notice in the Description page of Project Settings.

#include "UAVSimulator/Util/AerodynamicUtil.h"
#include "Runtime/Core/Public/Math/Vector.h"


Chord AerodynamicUtil::FindChord(TArray<FAerodynamicProfileStructure> Points, FVector Offset)
{
     FAerodynamicProfileStructure MinXPoint = Points[0];
     FAerodynamicProfileStructure MaxXPoint = Points[0];

     for (int32 i = 1; i < Points.Num(); ++i)
     {
         if (Points[i].X < MinXPoint.X)
         {
             MinXPoint = Points[i];
         }
         if (Points[i].X > MaxXPoint.X)
         {
             MaxXPoint = Points[i];
         }
     }

     FVector MinPosition = FVector(MinXPoint.X, 0, MinXPoint.Z) + Offset;
     FVector MaxPosition = FVector(MaxXPoint.X, 0, MaxXPoint.Z) + Offset;
     return Chord(MinPosition, MaxPosition);
}

Chord AerodynamicUtil::FindChord(TArray<FVector> Points) {
    FVector MinXPoint = Points[0];
    FVector MaxXPoint = Points[0];

    for (int32 i = 1; i < Points.Num(); ++i)
    {
        if (Points[i].X < MinXPoint.X)
        {
            MinXPoint = Points[i];
        }
        if (Points[i].X > MaxXPoint.X)
        {
            MaxXPoint = Points[i];
        }
    }


    return Chord(MaxXPoint, MinXPoint);
}

TArray<FAerodynamicProfileStructure> AerodynamicUtil::Scale(const TArray<FAerodynamicProfileStructure> Points, float ScaleFactor)
{
    TArray<FAerodynamicProfileStructure> ScaledPoints;
    ScaledPoints.Reserve(Points.Num());

    Chord ChordPosition = FindChord(Points);
    const FVector Pivot = FVector(
        (ChordPosition.StartPoint.X + ChordPosition.EndPoint.X) / 2.0f,
        0.0f,
        (ChordPosition.StartPoint.Z + ChordPosition.EndPoint.Z) / 2.0f
    );

    for (const FAerodynamicProfileStructure& Point : Points)
    {
        float RelativeX = Point.X - Pivot.X;
        float RelativeZ = Point.Z - Pivot.Z;

        RelativeX *= ScaleFactor; 
        RelativeZ *= ScaleFactor; 

        FAerodynamicProfileStructure ScaledPoint;
        ScaledPoint.X = Pivot.X + RelativeX;
        ScaledPoint.Z = Pivot.Z + RelativeZ;

        ScaledPoints.Add(ScaledPoint);
    }

    return ScaledPoints;
}

TArray<FAerodynamicProfileStructure> AerodynamicUtil::NormalizePoints(TArray<FAerodynamicProfileStructure> Points)
{
    TArray<FAerodynamicProfileStructure> NormalizedPoints;
    for (FAerodynamicProfileStructure Point : Points) {
        FAerodynamicProfileStructure NormalizedPoint = FAerodynamicProfileStructure();
        NormalizedPoint.X = -Point.X;
        NormalizedPoint.Z = Point.Z;
        NormalizedPoints.Add(NormalizedPoint);
    }
    return NormalizedPoints;
}


TArray<FVector> AerodynamicUtil::ConvertTo3DPoints(TArray<FAerodynamicProfileStructure> Profile, float ChordLength, float ExpectedChordLength, FVector Offset)
{
    TArray<FAerodynamicProfileStructure> ScaledProfilePoints = AerodynamicUtil::Scale(Profile, ExpectedChordLength / ChordLength);
    return AdaptTo(ScaledProfilePoints, Offset);
}

TArray<FVector> AerodynamicUtil::AdaptTo(TArray<FAerodynamicProfileStructure> Points, FVector Offset)
{
    TArray<FVector> VectorsArray = TArray<FVector>();
    for (FAerodynamicProfileStructure Point : Points) {
        VectorsArray.Add(FVector(Point.X, 0.f, Point.Z) + Offset);
    }
    return VectorsArray;
}

TArray<FVector> AerodynamicUtil::ConvertToWorldCoordinates(USceneComponent* Component, TArray<FVector> LocalCoordinates)
{
    TArray<FVector> WorldCoordinates;
    WorldCoordinates.Reserve(LocalCoordinates.Num());
    const FTransform& ComponentWorldTransform = Component->GetComponentTransform();
    for (const FVector& LocalPos : LocalCoordinates)
    {
        WorldCoordinates.Add(ComponentWorldTransform.TransformPosition(LocalPos));
    }

    return WorldCoordinates;
}