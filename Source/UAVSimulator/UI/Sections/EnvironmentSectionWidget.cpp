#include "EnvironmentSectionWidget.h"
#include "CesiumGeoreference.h"
#include "CesiumSunSky.h"
#include "Components/SpinBox.h"
#include "Kismet/GameplayStatics.h"

void UEnvironmentSectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SpinBoxOriginLatitude->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginLatitudeCommitted);
	SpinBoxOriginLongitude->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginLongitudeCommitted);
	SpinBoxOriginHeight->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnOriginHeightCommitted);
	SpinBoxTimeZone->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnTimeZoneCommitted);
	SpinBoxSolarTime->OnValueCommitted.AddDynamic(this, &UEnvironmentSectionWidget::OnSolarTimeCommitted);

	SyncFromWorld();
}

void UEnvironmentSectionWidget::OnSectionActivated_Implementation()
{
	SyncFromWorld();
}

void UEnvironmentSectionWidget::SyncFromWorld()
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
	{
		SpinBoxOriginLatitude->SetValue((float)Geo->GetOriginLatitude());
		SpinBoxOriginLongitude->SetValue((float)Geo->GetOriginLongitude());
		SpinBoxOriginHeight->SetValue((float)Geo->GetOriginHeight());
	}

	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SpinBoxTimeZone->SetValue((float)SunSky->TimeZone);
		SpinBoxSolarTime->SetValue((float)SunSky->SolarTime);
	}
}

void UEnvironmentSectionWidget::OnOriginLatitudeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
		Geo->SetOriginLatitude((double)Value);
}

void UEnvironmentSectionWidget::OnOriginLongitudeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
		Geo->SetOriginLongitude((double)Value);
}

void UEnvironmentSectionWidget::OnOriginHeightCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumGeoreference* Geo = GetGeoreference())
		Geo->SetOriginHeight((double)Value);
}

void UEnvironmentSectionWidget::OnTimeZoneCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->TimeZone = (double)Value;
		SunSky->UpdateSun();
	}
}

void UEnvironmentSectionWidget::OnSolarTimeCommitted(float Value, ETextCommit::Type /*CommitType*/)
{
	if (ACesiumSunSky* SunSky = GetSunSky())
	{
		SunSky->SolarTime = (double)Value;
		SunSky->UpdateSun();
	}
}

ACesiumGeoreference* UEnvironmentSectionWidget::GetGeoreference() const
{
	if (UWorld* World = GetWorld())
		return Cast<ACesiumGeoreference>(UGameplayStatics::GetActorOfClass(World, ACesiumGeoreference::StaticClass()));
	return nullptr;
}

ACesiumSunSky* UEnvironmentSectionWidget::GetSunSky() const
{
	if (UWorld* World = GetWorld())
		return Cast<ACesiumSunSky>(UGameplayStatics::GetActorOfClass(World, ACesiumSunSky::StaticClass()));
	return nullptr;
}
