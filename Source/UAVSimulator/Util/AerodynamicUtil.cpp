// Fill out your copyright notice in the Description page of Project Settings.

#include "UAVSimulator/Util/AerodynamicUtil.h"



Chord AerodynamicUtil::FindChord(TArray<FAirfoilPointData> Points, FVector Offset)
{
    FAirfoilPointData MinXPoint = Points[0];
    FAirfoilPointData MaxXPoint = Points[0];

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

TArray<FAirfoilPointData> AerodynamicUtil::Scale(const TArray<FAirfoilPointData> Points, float ScaleFactor)
{
    TArray<FAirfoilPointData> ScaledPoints;
    ScaledPoints.Reserve(Points.Num());

    Chord ChordPosition = FindChord(Points);
    const FVector Pivot = FVector(
        (ChordPosition.StartPoint.X + ChordPosition.EndPoint.X) / 2.0f,
        0.0f,
        (ChordPosition.StartPoint.Z + ChordPosition.EndPoint.Z) / 2.0f
    );

    for (const FAirfoilPointData& Point : Points)
    {
        float RelativeX = Point.X - Pivot.X;
        float RelativeZ = Point.Z - Pivot.Z;

        RelativeX *= ScaleFactor; 
        RelativeZ *= ScaleFactor; 

        FAirfoilPointData ScaledPoint;
        ScaledPoint.X = Pivot.X + RelativeX;
        ScaledPoint.Z = Pivot.Z + RelativeZ;

        ScaledPoints.Add(ScaledPoint);
    }

    return ScaledPoints;
}

TArray<FAirfoilPointData> AerodynamicUtil::NormalizePoints(TArray<FAirfoilPointData> Points)
{
    TArray<FAirfoilPointData> NormalizedPoints;
    for (FAirfoilPointData Point : Points) {
        FAirfoilPointData NormalizedPoint = FAirfoilPointData();
        NormalizedPoint.X = -Point.X;
        NormalizedPoint.Z = Point.Z;
        NormalizedPoints.Add(NormalizedPoint);
    }
    return NormalizedPoints;
}


TArray<FVector> AerodynamicUtil::ConvertTo3DPoints(TArray<FAirfoilPointData> Profile, float ChordLength, float ExpectedChordLength, FVector Offset)
{
    TArray<FAirfoilPointData> ScaledProfilePoints = AerodynamicUtil::Scale(Profile, ExpectedChordLength / ChordLength);
    return AdaptTo(ScaledProfilePoints, Offset);
}

TArray<FVector> AerodynamicUtil::AdaptTo(TArray<FAirfoilPointData> Points, FVector Offset)
{
    TArray<FVector> VectorsArray = TArray<FVector>();
    for (FAirfoilPointData Point : Points) {
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

FVector AerodynamicUtil::ConvertToWorldCoordinate(USceneComponent* Component, FVector LocalCoordinate)
{
    const FTransform& ComponentWorldTransform = Component->GetComponentTransform();
    return ComponentWorldTransform.TransformPosition(LocalCoordinate);
}

Chord AerodynamicUtil::ConvertChordToWorldCoordinate(USceneComponent* Component, Chord LocalChord)
{
    return Chord(ConvertToWorldCoordinate(Component, LocalChord.StartPoint), ConvertToWorldCoordinate(Component, LocalChord.EndPoint));
}