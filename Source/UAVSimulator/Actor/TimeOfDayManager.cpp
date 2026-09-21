#include "TimeOfDayManager.h"
#include "CesiumSunSky.h"
#include "Kismet/GameplayStatics.h"

ATimeOfDayManager::ATimeOfDayManager()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* DefaultRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRoot"));
	RootComponent = DefaultRoot;
}

void ATimeOfDayManager::SetTimeSpeed(float NewTimeSpeed)
{
	TimeSpeed = FMath::Max(0.0f, NewTimeSpeed);
	if (TimeSpeed <= 0.0f)
	{
		PendingHours = 0.0;
	}
}

void ATimeOfDayManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (TimeSpeed <= 0.0f)
	{
		return;
	}

	ACesiumSunSky* SunSky = GetSunSky();
	// Прихований SunSky означає вимкнений ландшафт Cesium (ApplyTerrainSurfaceState) —
	// замість нього працює резервне небо, рухати нема чого.
	if (!SunSky || SunSky->IsHidden())
	{
		return;
	}

	PendingHours += (double)DeltaTime * (double)TimeSpeed / 3600.0;
	TimeSinceLastUpdate += DeltaTime;
	if (TimeSinceLastUpdate < UpdateInterval)
	{
		return;
	}
	TimeSinceLastUpdate = 0.0f;

	SunSky->SolarTime = FMath::Fmod(SunSky->SolarTime + PendingHours, 24.0);
	PendingHours = 0.0;

	SunSky->UpdateSun();
	OnSolarTimeAdvanced.Broadcast(SunSky->SolarTime);
}

ACesiumSunSky* ATimeOfDayManager::GetSunSky()
{
	if (!CachedSunSky.IsValid())
	{
		CachedSunSky = Cast<ACesiumSunSky>(UGameplayStatics::GetActorOfClass(this, ACesiumSunSky::StaticClass()));
	}
	return CachedSunSky.Get();
}
