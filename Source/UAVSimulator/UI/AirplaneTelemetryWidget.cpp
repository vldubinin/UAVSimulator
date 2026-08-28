#include "AirplaneTelemetryWidget.h"
#include "UAVSimulator/Actor/Airplane.h"

void UAirplaneTelemetryWidget::SetAirplane(AAirplane* InAirplane)
{
	Airplane = InAirplane;
}

float UAirplaneTelemetryWidget::GetAltitudeMeters() const
{
	return Airplane ? Airplane->GetActorLocation().Z * 0.01f : 0.0f;
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

float UAirplaneTelemetryWidget::GetPitchDeg() const
{
	return Airplane ? Airplane->GetActorRotation().Pitch : 0.0f;
}

float UAirplaneTelemetryWidget::GetRollDeg() const
{
	return Airplane ? Airplane->GetActorRotation().Roll : 0.0f;
}
