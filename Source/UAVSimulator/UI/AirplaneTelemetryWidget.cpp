#include "AirplaneTelemetryWidget.h"
#include "UAVSimulator/Actor/Airplane.h"
#include "CesiumGeoreference.h"
#include "Components/TextBlock.h"

void UAirplaneTelemetryWidget::SetAirplane(AAirplane* InAirplane)
{
	Airplane = InAirplane;
}

void UAirplaneTelemetryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (SpeedValueText)
		SpeedValueText->SetText(FText::FromString(FString::Printf(TEXT("%.0f км/год"), GetAirspeedKmh())));

	if (AltitudeValueText)
		AltitudeValueText->SetText(FText::FromString(FString::Printf(TEXT("%.0f м"), GetAltitudeMeters())));

	if (ThrottleValueText)
		ThrottleValueText->SetText(FText::FromString(FString::Printf(TEXT("%.0f %%"), GetThrottlePercent())));

	if (ThrustValueText)
		ThrustValueText->SetText(FText::FromString(FString::Printf(TEXT("%.0f Н"), GetThrustN())));

	if (PitchValueText)
		PitchValueText->SetText(FText::FromString(FString::Printf(TEXT("%.1f°"), GetPitchDeg())));

	if (RollValueText)
		RollValueText->SetText(FText::FromString(FString::Printf(TEXT("%.1f°"), GetRollDeg())));
}

float UAirplaneTelemetryWidget::GetAltitudeMeters() const
{
	if (!Airplane)
		return 0.0f;

	if (!CachedGeoreference.IsValid())
		CachedGeoreference = ACesiumGeoreference::GetDefaultGeoreference(Airplane);

	if (const ACesiumGeoreference* Georeference = CachedGeoreference.Get())
	{
		// Та сама конвертація, що в UGeoPositionDroneComponent: Unreal -> Georeference-local -> LLH.
		const FVector LocalPositionCm = Georeference->GetActorTransform().InverseTransformPosition(Airplane->GetActorLocation());
		return (float)Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(LocalPositionCm).Z;
	}

	return Airplane->GetActorLocation().Z * 0.01f;
}

float UAirplaneTelemetryWidget::GetAirspeedMs() const
{
	// Через AAirplane::GetAirspeedMs() → FlightDynamics (швидкість фізичного тіла фюзеляжу).
	// Actor->GetVelocity() тут давало хибне значення: корінь актора не симулює фізику.
	return Airplane ? Airplane->GetAirspeedMs() : 0.0f;
}

float UAirplaneTelemetryWidget::GetAirspeedKmh() const
{
	return Airplane ? Airplane->GetAirspeedKmh() : 0.0f;
}

float UAirplaneTelemetryWidget::GetThrottlePercent() const
{
	return Airplane ? Airplane->GetThrottle01() * 100.0f : 0.0f;
}

float UAirplaneTelemetryWidget::GetThrustN() const
{
	return Airplane ? Airplane->GetThrustN() : 0.0f;
}

float UAirplaneTelemetryWidget::GetPitchDeg() const
{
	return Airplane ? Airplane->GetActorRotation().Pitch : 0.0f;
}

float UAirplaneTelemetryWidget::GetRollDeg() const
{
	return Airplane ? Airplane->GetActorRotation().Roll : 0.0f;
}
