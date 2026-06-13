#include "KeyPointDetectionComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/SensorUtilityLibrary.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

UKeyPointDetectionComponent::UKeyPointDetectionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UKeyPointDetectionComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	CaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	if (!CaptureComponent)
	{
		UE_LOG(LogUAV, Error, TEXT("KeyPointDetectionComponent: USceneCaptureComponent2D not found on %s."), *Owner->GetName());
		SetComponentTickEnabled(false);
		return;
	}

	// Try to derive vertical FOV now; if TextureTarget is not yet set by
	// UAVCameraComponent::BeginPlay, this will be retried lazily in TickComponent.
	if (CaptureComponent->TextureTarget)
	{
		const int32 W = CaptureComponent->TextureTarget->SizeX;
		const int32 H = CaptureComponent->TextureTarget->SizeY;
		SizeX = W;
		SizeY = H;
		if (W > 0 && H > 0)
		{
			const float AR     = static_cast<float>(W) / static_cast<float>(H);
			const float HFovRad = FMath::DegreesToRadians(CaptureComponent->FOVAngle);
			VerticalFOVDeg = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HFovRad * 0.5f) / AR));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UKeyPointDetectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled || !CaptureComponent) return;

	// UAVCameraComponent assigns TextureTarget in its own BeginPlay; re-read until valid.
	if ((SizeX <= 0 || SizeY <= 0) && CaptureComponent->TextureTarget)
	{
		SizeX = CaptureComponent->TextureTarget->SizeX;
		SizeY = CaptureComponent->TextureTarget->SizeY;
	}
	if (SizeX <= 0 || SizeY <= 0) return;

	// Recompute vertical FOV once the texture size is known.
	if (VerticalFOVDeg <= 0.0f && SizeX > 0 && SizeY > 0)
	{
		const float AR      = static_cast<float>(SizeX) / static_cast<float>(SizeY);
		const float HFovRad = FMath::DegreesToRadians(CaptureComponent->FOVAngle);
		VerticalFOVDeg = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HFovRad * 0.5f) / AR));
		if (VerticalFOVDeg <= 0.0f)
			VerticalFOVDeg = CaptureComponent->FOVAngle;
	}

	// ── 1. Discover nearby target actors via the same ray sweep as BBoxDetection ─
	TArray<FHitResult> HitResults = USensorUtilityLibrary::FindActors(
		this,
		CaptureComponent->GetComponentTransform(),
		GetOwner(),
		Range,
		HorizontalRays,
		VerticalLayers,
		VerticalFOVDeg,
		CollisionChannel
	);

	TArray<AActor*> TargetActors;
	for (const FHitResult& Hit : HitResults)
	{
		if (Hit.GetActor())
			TargetActors.AddUnique(Hit.GetActor());
	}

	// ── 2. For each target actor collect and project its UKeyPointComponents ───
	TMap<FString, TArray<FKeyPoint2D>> PerActorKeyPoints;

	for (AActor* Target : TargetActors)
	{
		if (!IsValid(Target)) continue;

		TArray<UKeyPointComponent*> KPComps;
		Target->GetComponents<UKeyPointComponent>(KPComps);
		if (KPComps.IsEmpty()) continue;

		TArray<FKeyPoint2D>& KPList = PerActorKeyPoints.Add(Target->GetName());
		KPList.Reserve(KPComps.Num());

		for (const UKeyPointComponent* KP : KPComps)
		{
			if (!IsValid(KP)) continue;
			FKeyPoint2D Point;
			Point.ID       = KP->PointID;
			Point.bVisible = ProjectWorldToScreen(KP->GetComponentLocation(), Point.ScreenPosition);
			KPList.Add(MoveTemp(Point));
		}
	}

	// ── 3. Serialize and store ────────────────────────────────────────────────
	FString Json = SerializeAllKeyPoints(PerActorKeyPoints);
	FTCHARToUTF8 JsonUtf8(*Json);

	LatestPayload.Reset();
	LatestPayload.Append(reinterpret_cast<const uint8*>(JsonUtf8.Get()), JsonUtf8.Length());
	LatestTimestamp = GetWorld()->GetTimeSeconds();
	bHasFrame = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface
// ─────────────────────────────────────────────────────────────────────────────

bool UKeyPointDetectionComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bHasFrame) return false;

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestTimestamp;
	OutFrame.Payload   = LatestPayload;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Projection — identical view/projection setup to BBoxDetectionComponent.
// ─────────────────────────────────────────────────────────────────────────────

bool UKeyPointDetectionComponent::ProjectWorldToScreen(const FVector& WorldPos, FVector2D& OutScreenPos) const
{
	if (!CaptureComponent || SizeX <= 0 || SizeY <= 0)
		return false;

	const float AspectRatio = static_cast<float>(SizeX) / static_cast<float>(SizeY);

	const FTransform CaptureTransform = CaptureComponent->GetComponentTransform();
	const FVector    ViewLocation     = CaptureTransform.GetLocation();
	const FRotator   ViewRotation     = CaptureTransform.GetRotation().Rotator();

	FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewRotation);
	FMatrix ViewMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix *
		FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1)
		);

	FMatrix ProjectionMatrix;
	if (CaptureComponent->bUseCustomProjectionMatrix)
	{
		ProjectionMatrix = CaptureComponent->CustomProjectionMatrix;
	}
	else if (CaptureComponent->ProjectionType == ECameraProjectionMode::Perspective)
	{
		const float FOV = CaptureComponent->FOVAngle * (float)PI / 360.0f;
		ProjectionMatrix = FReversedZPerspectiveMatrix(FOV, AspectRatio, 1.0f, GNearClippingPlane);
	}
	else
	{
		const float OrthoWidth  = CaptureComponent->OrthoWidth / 2.0f;
		const float OrthoHeight = OrthoWidth / AspectRatio;
		ProjectionMatrix = FReversedZOrthoMatrix(OrthoWidth, OrthoHeight, 0.5f / OrthoWidth, GNearClippingPlane);
	}

	const FMatrix  ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
	const FVector4 Projected            = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPos, 1.f));

	if (Projected.W <= 0.0f)
	{
		OutScreenPos = FVector2D::ZeroVector;
		return false;
	}

	const float RHW     = 1.0f / Projected.W;
	const float ScreenX = (Projected.X * RHW + 1.0f) * (SizeX * 0.5f);
	const float ScreenY = (1.0f - Projected.Y * RHW) * (SizeY * 0.5f);

	OutScreenPos = FVector2D(ScreenX, ScreenY);

	return ScreenX >= 0.0f && ScreenX <= static_cast<float>(SizeX) &&
	       ScreenY >= 0.0f && ScreenY <= static_cast<float>(SizeY);
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialization — one object keyed by actor name, analogous to BBox output.
// ─────────────────────────────────────────────────────────────────────────────

FString UKeyPointDetectionComponent::SerializeAllKeyPoints(const TMap<FString, TArray<FKeyPoint2D>>& PerActorKeyPoints) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	for (const auto& ActorPair : PerActorKeyPoints)
	{
		TArray<TSharedPtr<FJsonValue>> KPArray;
		KPArray.Reserve(ActorPair.Value.Num());

		for (const FKeyPoint2D& KP : ActorPair.Value)
		{
			TSharedRef<FJsonObject> KPObj = MakeShared<FJsonObject>();
			KPObj->SetStringField(TEXT("id"),      KP.ID);
			KPObj->SetNumberField(TEXT("x"),       KP.ScreenPosition.X);
			KPObj->SetNumberField(TEXT("y"),       KP.ScreenPosition.Y);
			KPObj->SetBoolField  (TEXT("visible"), KP.bVisible);
			KPArray.Add(MakeShared<FJsonValueObject>(KPObj));
		}

		Root->SetArrayField(ActorPair.Key, KPArray);
	}

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}
