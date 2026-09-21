#include "FogManager.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/UAVSimulator.h"

AFogManager::AFogManager()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* DefaultRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRoot"));
	RootComponent = DefaultRoot;
}

void AFogManager::BeginPlay()
{
	Super::BeginPlay();

	// Застосовуємо початковий стан (0 = вимкнено) одразу — інакше туман, що вже стоїть у рівні,
	// лишився б увімкненим, доки меню не викличе SetFogIntensity.
	ApplyFog();
}

void AFogManager::SetFogIntensity(float NewIntensity)
{
	FogIntensity = FMath::Clamp(NewIntensity, 0.0f, 100.0f);
	ApplyFog();
}

AExponentialHeightFog* AFogManager::GetOrCreateFogActor()
{
	if (CachedFogActor.IsValid())
	{
		return CachedFogActor.Get();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AExponentialHeightFog* Fog = Cast<AExponentialHeightFog>(
		UGameplayStatics::GetActorOfClass(World, AExponentialHeightFog::StaticClass()));
	if (!Fog)
	{
		Fog = World->SpawnActor<AExponentialHeightFog>(GetActorLocation(), FRotator::ZeroRotator);
		UE_LOG(LogUAV, Log, TEXT("FogManager: no AExponentialHeightFog in level — spawned %s"),
			Fog ? *Fog->GetName() : TEXT("FAILED"));
	}

	CachedFogActor = Fog;
	return Fog;
}

void AFogManager::ApplyFog()
{
	AExponentialHeightFog* Fog = GetOrCreateFogActor();
	UExponentialHeightFogComponent* FogComp = Fog ? Fog->GetComponent() : nullptr;
	if (!FogComp)
	{
		return;
	}

	if (FogIntensity <= 0.0f)
	{
		FogComp->SetVisibility(false);
		return;
	}

	const float Alpha = FogIntensity / 100.0f;
	FogComp->SetVisibility(true);
	FogComp->SetFogDensity(MaxFogDensity * Alpha * Alpha);
	FogComp->SetFogHeightFalloff(FogHeightFalloff);
	FogComp->SetFogMaxOpacity(1.0f);
	FogComp->SetStartDistance(FogStartDistance);
	FogComp->SetVolumetricFog(bUseVolumetricFog);
}
