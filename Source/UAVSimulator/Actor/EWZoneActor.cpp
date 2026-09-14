// Fill out your copyright notice in the Description page of Project Settings.

#include "EWZoneActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "CesiumGeoreference.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/UAVSimulator.h"

AEWZoneActor::AEWZoneActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SphereVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SphereVisual"));
	RootComponent = SphereVisual;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshAsset.Succeeded())
		SphereVisual->SetStaticMesh(SphereMeshAsset.Object);

	SphereVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SphereVisual->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereVisual->SetGenerateOverlapEvents(false);
	SphereVisual->CastShadow = false;
	SphereVisual->SetMobility(EComponentMobility::Movable);
}

void AEWZoneActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyRadius();

	if (ZoneMaterial)
		SphereVisual->SetMaterial(0, ZoneMaterial);
}

void AEWZoneActor::BeginPlay()
{
	Super::BeginPlay();

	// Одноразова ініціалізація StoredLongitude/Latitude/Height зі стартової Unreal-позиції
	// (розміщення в редакторі або (0,0,0) від SpawnActor) — це єдине місце, де ми
	// реконвертуємо Unreal-позицію в LLH, і робимо це з валідної, близької до origin точки.
	// Далі SetLongitude/SetLatitude завжди рахують позицію ВПЕРЕД з цих полів, ніколи назад.
	ACesiumGeoreference* Geo = ResolveGeoreference();
	if (!Geo)
	{
		UE_LOG(LogUAV, Warning, TEXT("EWZoneActor[%s]::BeginPlay: no ACesiumGeoreference found in world — Stored LLH stays (0,0,0)."), *GetName());
		return;
	}

	const FVector StartLocation = GetActorLocation();
	const FVector LocalPosition = Geo->GetActorTransform().InverseTransformPosition(StartLocation);
	const FVector LLH = Geo->TransformUnrealPositionToLongitudeLatitudeHeight(LocalPosition);
	StoredLongitude = LLH.X;
	StoredLatitude  = LLH.Y;
	StoredHeight    = LLH.Z;

	UE_LOG(LogUAV, Log,
		TEXT("EWZoneActor[%s]::BeginPlay: Georeference=%s OriginLLH=(%f,%f,%f) GeoActorTransform.Loc=%s StartActorLocation=%s -> LocalPosition=%s -> StoredLLH=(%f,%f,%f)"),
		*GetName(), *Geo->GetName(),
		Geo->GetOriginLongitude(), Geo->GetOriginLatitude(), Geo->GetOriginHeight(),
		*Geo->GetActorTransform().GetLocation().ToString(),
		*StartLocation.ToString(), *LocalPosition.ToString(),
		StoredLongitude, StoredLatitude, StoredHeight);
}

void AEWZoneActor::SetRadius(float NewRadius)
{
	Radius = NewRadius;
	ApplyRadius();
}

void AEWZoneActor::ApplyRadius()
{
	const float Scale = FMath::Max(Radius, 1.0f) / BaseSphereRadiusCm;
	SphereVisual->SetWorldScale3D(FVector(Scale));
}

ACesiumGeoreference* AEWZoneActor::ResolveGeoreference() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	TArray<AActor*> AllGeoreferences;
	UGameplayStatics::GetAllActorsOfClass(World, ACesiumGeoreference::StaticClass(), AllGeoreferences);
	if (AllGeoreferences.Num() > 1)
	{
		UE_LOG(LogUAV, Warning, TEXT("EWZoneActor[%s]::ResolveGeoreference: %d ACesiumGeoreference actors found (expected 1) — using the first:"), *GetName(), AllGeoreferences.Num());
		for (AActor* A : AllGeoreferences)
		{
			if (ACesiumGeoreference* G = Cast<ACesiumGeoreference>(A))
				UE_LOG(LogUAV, Warning, TEXT("  - %s OriginLLH=(%f,%f,%f)"), *G->GetName(), G->GetOriginLongitude(), G->GetOriginLatitude(), G->GetOriginHeight());
		}
	}

	return AllGeoreferences.Num() > 0 ? Cast<ACesiumGeoreference>(AllGeoreferences[0]) : nullptr;
}

void AEWZoneActor::ApplyGeoPosition()
{
	ACesiumGeoreference* Geo = ResolveGeoreference();
	if (!Geo)
	{
		UE_LOG(LogUAV, Warning, TEXT("EWZoneActor[%s]::ApplyGeoPosition: no ACesiumGeoreference found — position NOT applied. StoredLLH=(%f,%f,%f)"),
			*GetName(), StoredLongitude, StoredLatitude, StoredHeight);
		return;
	}

	const FVector LocalPosition = Geo->TransformLongitudeLatitudeHeightPositionToUnreal(
		FVector(StoredLongitude, StoredLatitude, StoredHeight));
	const FVector WorldPosition = Geo->GetActorTransform().TransformPosition(LocalPosition);
	SetActorLocation(WorldPosition);

	UE_LOG(LogUAV, Log,
		TEXT("EWZoneActor[%s]::ApplyGeoPosition: Georeference=%s OriginLLH=(%f,%f,%f) StoredLLH=(%f,%f,%f) -> LocalPosition=%s -> WorldPosition(applied)=%s -> GetActorLocation()(readback)=%s"),
		*GetName(), *Geo->GetName(),
		Geo->GetOriginLongitude(), Geo->GetOriginLatitude(), Geo->GetOriginHeight(),
		StoredLongitude, StoredLatitude, StoredHeight,
		*LocalPosition.ToString(), *WorldPosition.ToString(), *GetActorLocation().ToString());
}

void AEWZoneActor::SetLongitude(double NewLongitude)
{
	UE_LOG(LogUAV, Log, TEXT("EWZoneActor[%s]::SetLongitude: %f -> %f"), *GetName(), StoredLongitude, NewLongitude);
	StoredLongitude = NewLongitude;
	ApplyGeoPosition();
}

void AEWZoneActor::SetLatitude(double NewLatitude)
{
	UE_LOG(LogUAV, Log, TEXT("EWZoneActor[%s]::SetLatitude: %f -> %f"), *GetName(), StoredLatitude, NewLatitude);
	StoredLatitude = NewLatitude;
	ApplyGeoPosition();
}
