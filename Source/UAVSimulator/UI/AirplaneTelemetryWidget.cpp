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
	return Airplane ? Airplane->GetVelocity().Size() * 0.01f : 0.0f;
}

float UAirplaneTelemetryWidget::GetPitchDeg() const
{
	return Airplane ? Airplane->GetActorRotation().Pitch : 0.0f;
}

float UAirplaneTelemetryWidget::GetRollDeg() const
{
	return Airplane ? Airplane->GetActorRotation().Roll : 0.0f;
}
