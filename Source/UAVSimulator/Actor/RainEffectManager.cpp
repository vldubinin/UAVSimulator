#include "RainEffectManager.h"
#include "UAVSimulator/Actor/Airplane.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

namespace
{
	// User Parameter (Float), який має бути експонований у NS_Rain і використовуватись
	// (типово — множником на Spawn Rate) — інакше SetRainIntensity() ні на що не вплине.
	const FName IntensityParameterName(TEXT("Intensity"));
}

ARainEffectManager::ARainEffectManager()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* DefaultRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRoot"));
	RootComponent = DefaultRoot;
}

void ARainEffectManager::BeginPlay()
{
	Super::BeginPlay();

	if (RainIntensity > 0.0f)
	{
		RescanAirplanes();
	}
}

void ARainEffectManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (RainIntensity <= 0.0f)
	{
		return;
	}

	TimeSinceLastRescan += DeltaTime;
	if (TimeSinceLastRescan >= RescanInterval)
	{
		TimeSinceLastRescan = 0.0f;
		RescanAirplanes();
	}

	UpdateRainPositions();
}

void ARainEffectManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyAllRainEffects();

	Super::EndPlay(EndPlayReason);
}

void ARainEffectManager::SetRainIntensity(float NewIntensity)
{
	const bool bWasEnabled = RainIntensity > 0.0f;
	RainIntensity = FMath::Max(NewIntensity, 0.0f);
	const bool bIsEnabled = RainIntensity > 0.0f;

	UE_LOG(LogUAV, Log, TEXT("RainEffectManager::SetRainIntensity: %.2f"), RainIntensity);

	if (!bIsEnabled)
	{
		DestroyAllRainEffects();
		return;
	}

	for (const TPair<TWeakObjectPtr<AAirplane>, TObjectPtr<UNiagaraComponent>>& Pair : ActiveRainEffects)
	{
		ApplyIntensity(Pair.Value);
	}

	if (!bWasEnabled)
	{
		TimeSinceLastRescan = 0.0f;
		RescanAirplanes();
	}
}

void ARainEffectManager::ApplyIntensity(UNiagaraComponent* RainComp) const
{
	if (RainComp)
	{
		RainComp->SetFloatParameter(IntensityParameterName, RainIntensity);
	}
}

void ARainEffectManager::DestroyAllRainEffects()
{
	for (const TPair<TWeakObjectPtr<AAirplane>, TObjectPtr<UNiagaraComponent>>& Pair : ActiveRainEffects)
	{
		if (UNiagaraComponent* RainComp = Pair.Value)
		{
			RainComp->DestroyComponent();
		}
	}
	ActiveRainEffects.Empty();
}

void ARainEffectManager::RescanAirplanes()
{
	if (!RainSystem)
	{
		UE_LOG(LogUAV, Warning, TEXT("RainEffectManager::RescanAirplanes: RainSystem не задано — нічого не спавню"));
		return;
	}

	// Прибираємо записи для літаків, яких уже знищено (StopSimulation, зміна режиму тощо).
	for (auto It = ActiveRainEffects.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			UE_LOG(LogUAV, Log, TEXT("RainEffectManager::RescanAirplanes: літак знищено — прибираю RainComp %s"),
				It->Value ? *It->Value->GetName() : TEXT("<null>"));
			if (UNiagaraComponent* RainComp = It->Value)
			{
				RainComp->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}

	TArray<AActor*> FoundAirplanes;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAirplane::StaticClass(), FoundAirplanes);

	UE_LOG(LogUAV, Verbose, TEXT("RainEffectManager::RescanAirplanes: знайдено %d AAirplane, відстежується %d"),
		FoundAirplanes.Num(), ActiveRainEffects.Num());

	for (AActor* Actor : FoundAirplanes)
	{
		AAirplane* Airplane = Cast<AAirplane>(Actor);
		if (!Airplane || ActiveRainEffects.Contains(Airplane))
		{
			continue;
		}

		if (UNiagaraComponent* RainComp = SpawnRainForAirplane(Airplane))
		{
			ApplyIntensity(RainComp);
			UE_LOG(LogUAV, Log,
				TEXT("RainEffectManager::RescanAirplanes: заспавнив %s для %s на %s (Location=%s Rotation=%s Intensity=%.2f)"),
				*RainComp->GetName(), *Airplane->GetName(),
				*RainSystem->GetName(),
				*RainComp->GetComponentLocation().ToString(),
				*RainComp->GetComponentRotation().ToString(),
				RainIntensity);
			ActiveRainEffects.Add(Airplane, RainComp);
		}
	}
}

UNiagaraComponent* ARainEffectManager::SpawnRainForAirplane(const AAirplane* Airplane) const
{
	const FVector SpawnLocation = Airplane->GetActorLocation() + RainOffset;

	// bAutoDestroy = false — часом життя керує сам менеджер (EndPlay / RescanAirplanes),
	// а не система завершення партиклів (дощ — зациклений ефект, який сам ніколи не "завершується").
	return UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		RainSystem,
		SpawnLocation,
		FRotator::ZeroRotator,
		FVector(1.0f),
		false,
		true,
		ENCPoolMethod::None,
		true);
}

void ARainEffectManager::UpdateRainPositions() const
{
	for (const TPair<TWeakObjectPtr<AAirplane>, TObjectPtr<UNiagaraComponent>>& Pair : ActiveRainEffects)
	{
		const AAirplane* Airplane = Pair.Key.Get();
		UNiagaraComponent* RainComp = Pair.Value;
		if (!Airplane || !RainComp)
		{
			continue;
		}

		RainComp->SetWorldLocation(Airplane->GetActorLocation() + RainOffset);
	}
}
