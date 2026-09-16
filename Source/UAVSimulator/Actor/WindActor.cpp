#include "WindActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "CesiumGeoreference.h"
#include "Kismet/GameplayStatics.h"
#include "UAVSimulator/UAVSimulator.h"

AWindActor::AWindActor()
{
	PrimaryActorTick.bCanEverTick = false;

	BoxVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoxVisual"));
	RootComponent = BoxVisual;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshAsset.Succeeded())
		BoxVisual->SetStaticMesh(CubeMeshAsset.Object);

	BoxVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoxVisual->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoxVisual->SetGenerateOverlapEvents(false);
	BoxVisual->CastShadow = false;
	BoxVisual->SetMobility(EComponentMobility::Movable);

	ArrowVisual = CreateDefaultSubobject<UArrowComponent>(TEXT("ArrowVisual"));
	ArrowVisual->SetupAttachment(BoxVisual);
	ArrowVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// UArrowComponent defaults bHiddenInGame to true (it's normally an editor-only gizmo) —
	// this actor needs it visible at runtime too. bAbsoluteScale defaults to false, so being
	// a child of BoxVisual it already inherits BoxVisual's (non-uniform) scale as-is.
	ArrowVisual->SetHiddenInGame(false);
}

void AWindActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (WindMaterial)
		BoxVisual->SetMaterial(0, WindMaterial);
}

void AWindActor::BeginPlay()
{
	Super::BeginPlay();

	// На відміну від AEWZoneActor, цей актор ніколи не розміщується вручну в рівні —
	// його єдине джерело позиції це SetGeoPositions() від AEnvironmentActorManager, тож
	// тут нема сенсу реконвертувати стартову Unreal-позицію в LLH (як робить
	// AEWZoneActor::BeginPlay). Підписка лишається — щоб позиція самовиправилась, якщо
	// SetGeoPositions() викликали до того, як Cesium origin виставили остаточно.
	ACesiumGeoreference* Geo = ResolveGeoreference();
	if (!Geo)
	{
		UE_LOG(LogUAV, Warning, TEXT("WindActor[%s]::BeginPlay: no ACesiumGeoreference found in world."), *GetName());
		return;
	}

	Geo->OnGeoreferenceUpdated.AddDynamic(this, &AWindActor::OnGeoreferenceUpdated);
}

void AWindActor::OnGeoreferenceUpdated()
{
	UE_LOG(LogUAV, Log, TEXT("WindActor[%s]::OnGeoreferenceUpdated: origin changed — reapplying Stored Start/End LLH"), *GetName());
	ApplyTransform();
}

ACesiumGeoreference* AWindActor::ResolveGeoreference() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	TArray<AActor*> AllGeoreferences;
	UGameplayStatics::GetAllActorsOfClass(World, ACesiumGeoreference::StaticClass(), AllGeoreferences);
	return AllGeoreferences.Num() > 0 ? Cast<ACesiumGeoreference>(AllGeoreferences[0]) : nullptr;
}

void AWindActor::ApplyTransform()
{
	ACesiumGeoreference* Geo = ResolveGeoreference();
	if (!Geo)
	{
		UE_LOG(LogUAV, Warning, TEXT("WindActor[%s]::ApplyTransform: no ACesiumGeoreference found — position NOT applied."), *GetName());
		return;
	}

	const FVector StartLocal = Geo->TransformLongitudeLatitudeHeightPositionToUnreal(
		FVector(StoredStartLongitude, StoredStartLatitude, StoredStartHeight));
	const FVector EndLocal = Geo->TransformLongitudeLatitudeHeightPositionToUnreal(
		FVector(StoredEndLongitude, StoredEndLatitude, StoredEndHeight));

	const FVector StartWorld = Geo->GetActorTransform().TransformPosition(StartLocal);
	const FVector EndWorld = Geo->GetActorTransform().TransformPosition(EndLocal);

	const FVector Delta = EndWorld - StartWorld;
	const float Length = Delta.Size();
	const FRotator Rotation = Delta.IsNearlyZero() ? GetActorRotation() : Delta.Rotation();

	// BoxVisual — куб з центром в origin, тож ставимо актора в середину відрізка: куб
	// рівномірно розтягується в обидва боки й одна грань опиняється в StartWorld, інша — в EndWorld.
	SetActorLocationAndRotation((StartWorld + EndWorld) * 0.5, Rotation);

	// Бокс, що описує циліндр радіуса Radius: переріз (Y/Z) — квадрат зі стороною 2*Radius.
	const float LengthScale = FMath::Max(Length, 1.0f) / BaseCubeSizeCm;
	const float RadiusScale = FMath::Max(Radius, 1.0f) * 2.0f / BaseCubeSizeCm;
	BoxVisual->SetRelativeScale3D(FVector(LengthScale, RadiusScale, RadiusScale));
}

void AWindActor::SetGeoPositions(double NewStartLongitude, double NewStartLatitude, double NewStartHeight,
	double NewEndLongitude, double NewEndLatitude, double NewEndHeight)
{
	StoredStartLongitude = NewStartLongitude;
	StoredStartLatitude = NewStartLatitude;
	StoredStartHeight = NewStartHeight;
	StoredEndLongitude = NewEndLongitude;
	StoredEndLatitude = NewEndLatitude;
	StoredEndHeight = NewEndHeight;
	ApplyTransform();
}

void AWindActor::SetRadius(float NewRadius)
{
	Radius = NewRadius;
	ApplyTransform();
}

void AWindActor::SetSpeed(float NewSpeed)
{
	Speed = NewSpeed;
}

FVector AWindActor::GetWindVelocityAtLocation(const FVector& WorldLocation) const
{
	if (Speed <= 0.0f)
		return FVector::ZeroVector;

	// GetActorTransform() уже включає нерівномірний RelativeScale3D BoxVisual (він —
	// root-компонент), тож ця точка одразу опиняється в НЕмасштабованому просторі куба —
	// півсторона = BaseCubeSizeCm*0.5 на кожній осі, окремо кешувати Length/Radius не треба.
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float HalfSize = BaseCubeSizeCm * 0.5f;

	// Відстань Чебишева в нормалізованих (по кожній осі) одиницях: 1.0 — точно на грані боксу.
	const float Nx = FMath::Abs(Local.X) / HalfSize;
	const float Ny = FMath::Abs(Local.Y) / HalfSize;
	const float Nz = FMath::Abs(Local.Z) / HalfSize;
	const float D  = FMath::Max3(Nx, Ny, Nz);

	if (D >= 1.0f)
		return FVector::ZeroVector;

	const float InnerEdge = 1.0f - EdgeSoftnessFrac;
	float Factor = 1.0f;
	if (D > InnerEdge)
	{
		// Класичний smoothstep(0,1,t) — 3t² - 2t³, t = наскільки глибоко в пом'якшеній смузі.
		const float T = FMath::Clamp((D - InnerEdge) / EdgeSoftnessFrac, 0.0f, 1.0f);
		Factor = 1.0f - (T * T * (3.0f - 2.0f * T));
	}

	return GetActorForwardVector() * Speed * Factor;
}
