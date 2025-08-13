// Fill out your copyright notice in the Description page of Project Settings.

#include "Runtime/Core/Public/Math/Vector.h"
#include "UAVSimulator/Util/AerodynamicUtil.h"

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


    return Chord(MinXPoint, MaxXPoint);
}


// Приймаємо масив за константним посиланням (&), щоб уникнути непотрібного копіювання
TArray<FAerodynamicProfileStructure> AerodynamicUtil::Scale(const TArray<FAerodynamicProfileStructure> Points, float ScaleFactor)
{
    TArray<FAerodynamicProfileStructure> ScaledPoints;
    // Резервуємо місце в масиві для ефективності
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

        // ВИПРАВЛЕНО: Множимо відносні координати на коефіцієнт, а не перезаписуємо їх.
        RelativeX *= ScaleFactor; // Те саме, що RelativeX = RelativeX * ScaleFactor;
        RelativeZ *= ScaleFactor; // Те саме, що RelativeZ = RelativeZ * ScaleFactor;

        FAerodynamicProfileStructure ScaledPoint; // Немає потреби в FAerodynamicProfileStructure()
        ScaledPoint.X = Pivot.X + RelativeX;
        ScaledPoint.Z = Pivot.Z + RelativeZ;
        // Можна також скопіювати інші поля, якщо вони є
        // ScaledPoint.SomeOtherField = Point.SomeOtherField;

        ScaledPoints.Add(ScaledPoint);
    }

    return ScaledPoints;
}