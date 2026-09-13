#include "GeoPositionDroneComponent.h"

#include "CesiumGeoreference.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"

#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	/** Haversine — велике коло між двома точками (широта/довгота в градусах), метри. Лише для діагностичного логу. */
	double HaversineDistanceMeters(double Lat1Deg, double Lon1Deg, double Lat2Deg, double Lon2Deg)
	{
		constexpr double EarthRadiusM = 6371000.0;
		const double Lat1 = FMath::DegreesToRadians(Lat1Deg);
		const double Lat2 = FMath::DegreesToRadians(Lat2Deg);
		const double DLat = FMath::DegreesToRadians(Lat2Deg - Lat1Deg);
		const double DLon = FMath::DegreesToRadians(Lon2Deg - Lon1Deg);

		const double A = FMath::Sin(DLat / 2.0) * FMath::Sin(DLat / 2.0)
			+ FMath::Cos(Lat1) * FMath::Cos(Lat2) * FMath::Sin(DLon / 2.0) * FMath::Sin(DLon / 2.0);
		const double C = 2.0 * FMath::Atan2(FMath::Sqrt(A), FMath::Sqrt(1.0 - A));
		return EarthRadiusM * C;
	}
}

UGeoPositionDroneComponent::UGeoPositionDroneComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGeoPositionDroneComponent::BeginPlay()
{
	Super::BeginPlay();

	Georeference = ACesiumGeoreference::GetDefaultGeoreference(this);

	if (AActor* Owner = GetOwner())
	{
		FlightDynamics = Owner->FindComponentByClass<UFlightDynamicsComponent>();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UGeoPositionDroneComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled) return;

	AActor* Owner = GetOwner();
	if (!Owner || !Georeference) return;

	const FVector LocalPositionCm = Georeference->GetActorTransform().InverseTransformPosition(Owner->GetActorLocation());
	LatestLongitudeLatitudeHeight = Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(LocalPositionCm);
	LatestTimestamp                = GetWorld()->GetTimeSeconds();
	bHasData                       = true;

	UE_LOG(LogUAV, Log, TEXT("[SpeedDebug] GeoPosition Lat=%.7f Lon=%.7f Alt=%.3f m (Owner=%s)"),
		LatestLongitudeLatitudeHeight.Y,
		LatestLongitudeLatitudeHeight.X,
		LatestLongitudeLatitudeHeight.Z,
		*Owner->GetName());

	if (FlightDynamics)
	{
		const FVector LeftTipWorldCm  = FlightDynamics->GetLeftWingtipWorldPosition();
		const FVector RightTipWorldCm = FlightDynamics->GetRightWingtipWorldPosition();
		const float   WingSpanCm      = FVector::Dist(LeftTipWorldCm, RightTipWorldCm);

		const FVector LeftTipLocalCm  = Georeference->GetActorTransform().InverseTransformPosition(LeftTipWorldCm);
		const FVector RightTipLocalCm = Georeference->GetActorTransform().InverseTransformPosition(RightTipWorldCm);
		const FVector LeftTipGeo      = Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(LeftTipLocalCm);
		const FVector RightTipGeo     = Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(RightTipLocalCm);
		const double  WingSpanGeoM    = HaversineDistanceMeters(LeftTipGeo.Y, LeftTipGeo.X, RightTipGeo.Y, RightTipGeo.X);

		UE_LOG(LogUAV, Log, TEXT("[SpeedDebug] WingSpan Unreal=%.2f cm (%.2f m) | LeftTip(UE)=%s RightTip(UE)=%s | ")
			TEXT("LeftTip(Geo) Lat=%.7f Lon=%.7f Alt=%.2fm | RightTip(Geo) Lat=%.7f Lon=%.7f Alt=%.2fm | GeoSpan=%.2f m"),
			WingSpanCm, WingSpanCm / 100.0,
			*LeftTipWorldCm.ToString(), *RightTipWorldCm.ToString(),
			LeftTipGeo.Y, LeftTipGeo.X, LeftTipGeo.Z,
			RightTipGeo.Y, RightTipGeo.X, RightTipGeo.Z,
			WingSpanGeoM);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — викликається в ігровому потоці з SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool UGeoPositionDroneComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bHasData) return false;

	TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetNumberField(TEXT("latitude"),   LatestLongitudeLatitudeHeight.Y);
	JsonObj->SetNumberField(TEXT("longitude"),  LatestLongitudeLatitudeHeight.X);
	JsonObj->SetNumberField(TEXT("altitude_m"), LatestLongitudeLatitudeHeight.Z);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, Writer);

	FTCHARToUTF8 Utf8(*JsonString);

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestTimestamp;
	OutFrame.Payload.Reset();
	OutFrame.Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return true;
}
