// Fill out your copyright notice in the Description page of Project Settings.

#include "UAVSimulator/Util/AerodynamicUtil.h"


FChord AerodynamicUtil::FindChord(TArray<FAirfoilPointData> Points, FVector Offset)
{
	// Шукаємо крайні точки по осі X — це передня (MaxX) та задня (MinX) кромки
	FAirfoilPointData MinXPoint = Points[0];
	FAirfoilPointData MaxXPoint = Points[0];

	for (int32 i = 1; i < Points.Num(); ++i)
	{
		if (Points[i].X < MinXPoint.X) MinXPoint = Points[i];
		if (Points[i].X > MaxXPoint.X) MaxXPoint = Points[i];
	}

	// Зберігаємо Y = 0 (2D-профіль), додаємо зміщення
	FVector MinPosition = FVector(MinXPoint.X, 0, MinXPoint.Z) + Offset;
	FVector MaxPosition = FVector(MaxXPoint.X, 0, MaxXPoint.Z) + Offset;
	return FChord(MinPosition, MaxPosition);
}

FChord AerodynamicUtil::FindChord(TArray<FVector> Points)
{
	// Для 3D-масиву: StartPoint = MaxX (передня кромка), EndPoint = MinX (задня кромка)
	FVector MinXPoint = Points[0];
	FVector MaxXPoint = Points[0];

	for (int32 i = 1; i < Points.Num(); ++i)
	{
		if (Points[i].X < MinXPoint.X) MinXPoint = Points[i];
		if (Points[i].X > MaxXPoint.X) MaxXPoint = Points[i];
	}

	return FChord(MaxXPoint, MinXPoint);
}

TArray<FAirfoilPointData> AerodynamicUtil::Scale(const TArray<FAirfoilPointData> Points, float ScaleFactor)
{
	TArray<FAirfoilPointData> ScaledPoints;
	ScaledPoints.Reserve(Points.Num());

	// Масштабування відносно середини хорди як центру (не відносно початку координат)
	FChord ChordPosition = FindChord(Points);
	const FVector Pivot = FVector(
		(ChordPosition.StartPoint.X + ChordPosition.EndPoint.X) / 2.0f,
		0.0f,
		(ChordPosition.StartPoint.Z + ChordPosition.EndPoint.Z) / 2.0f
	);

	for (const FAirfoilPointData& Point : Points)
	{
		FAirfoilPointData ScaledPoint;
		ScaledPoint.X = Pivot.X + (Point.X - Pivot.X) * ScaleFactor;
		ScaledPoint.Z = Pivot.Z + (Point.Z - Pivot.Z) * ScaleFactor;
		ScaledPoints.Add(ScaledPoint);
	}

	return ScaledPoints;
}

TArray<FAirfoilPointData> AerodynamicUtil::NormalizePoints(TArray<FAirfoilPointData> Points)
{
	// Інверсія X приводить профіль у правостороннє представлення (передня кромка → +X)
	TArray<FAirfoilPointData> NormalizedPoints;
	for (FAirfoilPointData Point : Points)
	{
		FAirfoilPointData NormalizedPoint;
		NormalizedPoint.X = -Point.X;
		NormalizedPoint.Z =  Point.Z;
		NormalizedPoints.Add(NormalizedPoint);
	}
	return NormalizedPoints;
}

TArray<FVector> AerodynamicUtil::ConvertTo3DPoints(TArray<FAirfoilPointData> Profile, float ChordLength, float ExpectedChordLength, FVector Offset)
{
	// Масштабуємо профіль до цільової довжини хорди, потім переводимо в 3D
	TArray<FAirfoilPointData> ScaledProfile = AerodynamicUtil::Scale(Profile, ExpectedChordLength / ChordLength);
	return AdaptTo(ScaledProfile, Offset);
}

TArray<FVector> AerodynamicUtil::AdaptTo(TArray<FAirfoilPointData> Points, FVector Offset)
{
	// Встановлюємо Y = 0 (профіль лежить у площині XZ) і додаємо зміщення секції
	TArray<FVector> Result;
	for (FAirfoilPointData Point : Points)
	{
		Result.Add(FVector(Point.X, 0.f, Point.Z) + Offset);
	}
	return Result;
}
