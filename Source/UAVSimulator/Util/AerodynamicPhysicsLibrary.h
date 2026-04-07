#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UAVSimulator/Entity/Chord.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileRow.h"
#include "AerodynamicPhysicsLibrary.generated.h"

/**
 * Stateless aerodynamics math utilities.
 * Methods with Blueprint-compatible signatures are exposed as BlueprintCallable.
 * Methods taking FAerodynamicProfileRow* are plain static C++ only.
 */
UCLASS()
class UAVSIMULATOR_API UAerodynamicPhysicsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Angle of attack in degrees between airflow and chord direction, using the surface up vector. */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float CalculateAngleOfAttack(FVector WorldAirVelocity, FVector AverageChordDirection, FVector SurfaceUpVector);

	/** Lift force in Newtons from a polar profile. */
	static float CalculateLift(float AoA, float DynamicPressure, float SurfaceArea, FAerodynamicProfileRow* Profile);

	/** Drag force in Newtons from a polar profile. */
	static float CalculateDrag(float AoA, float DynamicPressure, float SurfaceArea, FAerodynamicProfileRow* Profile);

	/** Pitching moment torque in Newtons from a polar profile. */
	static float CalculateTorque(float AoA, float DynamicPressure, float SurfaceArea, float ChordLength, FAerodynamicProfileRow* Profile);

	/** Convert world-space air velocity vector magnitude from cm/s to m/s. */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float VelocityToMetersPerSecond(FVector WorldAirVelocity);

	/** Convert a force in Newtons to Unreal's kg·cm/s² unit. */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float NewtonsToKiloCentimeter(float Newtons);

	/** Average chord length (m) between two chords (chord points are in cm). */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float CalculateAverageChordLength(FChord FirstChord, FChord SecondChord);

	/** Lift direction as the cross product of span and drag directions. */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static FVector CalculateLiftDirection(FVector WorldAirVelocity, FVector SpanDirection);

	/** Center of pressure as a lerp along chord midlines at the given percentage offset. */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static FVector FindCenterOfPressure(FChord StartChord, FChord EndChord, float PercentageOffset);

	/** Area of the quadrilateral defined by two chords (two triangles). */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float CalculateQuadSurfaceArea(FChord StartChord, FChord EndChord);

	/** Linearly interpolate a point on a line segment at the given alpha [0,1]. */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static FVector GetPointOnLineAtPercentage(FVector StartPoint, FVector EndPoint, float Alpha);
};
