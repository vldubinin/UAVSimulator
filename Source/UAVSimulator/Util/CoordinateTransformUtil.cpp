#include "CoordinateTransformUtil.h"
#include "Components/SceneComponent.h"

TArray<FVector> CoordinateTransformUtil::LocalToWorld(USceneComponent* Component, const TArray<FVector>& LocalPositions)
{
	TArray<FVector> WorldPositions;
	WorldPositions.Reserve(LocalPositions.Num());
	const FTransform& T = Component->GetComponentTransform();
	for (const FVector& Local : LocalPositions)
	{
		WorldPositions.Add(T.TransformPosition(Local));
	}
	return WorldPositions;
}

FVector CoordinateTransformUtil::LocalToWorld(USceneComponent* Component, FVector LocalPosition)
{
	return Component->GetComponentTransform().TransformPosition(LocalPosition);
}

FChord CoordinateTransformUtil::ChordLocalToWorld(USceneComponent* Component, FChord LocalChord)
{
	const FTransform& T = Component->GetComponentTransform();
	return FChord(T.TransformPosition(LocalChord.StartPoint), T.TransformPosition(LocalChord.EndPoint));
}
