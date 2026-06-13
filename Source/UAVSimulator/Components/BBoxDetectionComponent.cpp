#include "BBoxDetectionComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Util/SensorUtilityLibrary.h"
#include "UAVSimulator/Structure/SensorFrame.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

UBBoxDetectionComponent::UBBoxDetectionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UBBoxDetectionComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	CaptureComponent = Owner->FindComponentByClass<USceneCaptureComponent2D>();
	if (!CaptureComponent)
	{
		UE_LOG(LogUAV, Error, TEXT("BBoxDetectionComponent: USceneCaptureComponent2D not found on %s."), *Owner->GetName());
		SetComponentTickEnabled(false);
		return;
	}

	// Derive vertical FOV from the capture component so the ray sweep matches the camera frustum.
	if (CaptureComponent->TextureTarget)
	{
		const int32 SizeX = CaptureComponent->TextureTarget->SizeX;
		const int32 SizeY = CaptureComponent->TextureTarget->SizeY;
		if (SizeX > 0 && SizeY > 0)
		{
			const float AspectRatio = static_cast<float>(SizeX) / static_cast<float>(SizeY);
			const float HFovRad = FMath::DegreesToRadians(CaptureComponent->FOVAngle);
			VerticalFOVDeg = FMath::RadiansToDegrees(
				2.0f * FMath::Atan(FMath::Tan(HFovRad * 0.5f) / AspectRatio)
			);
		}
	}

	// Fallback: use horizontal FOV as vertical FOV (square sensor assumption).
	if (VerticalFOVDeg <= 0.0f)
		VerticalFOVDeg = CaptureComponent->FOVAngle;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UBBoxDetectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled || !CaptureComponent) return;

	CollectSceneActors();

	TMap<FString, FBox2D> BBoxMap;
	for (AActor* Actor : SceneActors)
	{
		if (!IsValid(Actor)) continue;
		BBoxMap.Add(Actor->GetName(), ProjectActorToScreen(Actor));
	}

	FString Json = SerializeBBoxes(BBoxMap);
	FTCHARToUTF8 JsonUtf8(*Json);

	LatestPayload.Reset();
	LatestPayload.Append(reinterpret_cast<const uint8*>(JsonUtf8.Get()), JsonUtf8.Length());
	LatestTimestamp = GetWorld()->GetTimeSeconds();
	bHasFrame = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface
// ─────────────────────────────────────────────────────────────────────────────

bool UBBoxDetectionComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bHasFrame) return false;

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestTimestamp;
	OutFrame.Payload   = LatestPayload;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Actor collection — runs once; result is cached for subsequent ticks.
// ─────────────────────────────────────────────────────────────────────────────

void UBBoxDetectionComponent::CollectSceneActors()
{
	if (!SceneActors.IsEmpty()) return;

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

	for (const FHitResult& Hit : HitResults)
	{
		if (Hit.GetActor())
			SceneActors.AddUnique(Hit.GetActor());
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 2D projection — OBB mid-section approach to avoid perspective inflation.
// ─────────────────────────────────────────────────────────────────────────────

FBox2D UBBoxDetectionComponent::ProjectActorToScreen(AActor* Actor) const
{
	FBox2D Result;
	Result.bIsValid = false;

	if (!IsValid(CaptureComponent) || !IsValid(CaptureComponent->TextureTarget) || !IsValid(Actor))
		return Result;

	const int32 SizeX = CaptureComponent->TextureTarget->SizeX;
	const int32 SizeY = CaptureComponent->TextureTarget->SizeY;
	const float AspectRatio = static_cast<float>(SizeX) / static_cast<float>(SizeY);

	FTransform CaptureTransform = CaptureComponent->GetComponentTransform();
	FVector    ViewLocation     = CaptureTransform.GetLocation();
	FRotator   ViewRotation     = CaptureTransform.GetRotation().Rotator();

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
		float FOV = CaptureComponent->FOVAngle * (float)PI / 360.0f;
		ProjectionMatrix = FReversedZPerspectiveMatrix(FOV, AspectRatio, 1.0f, GNearClippingPlane);
	}
	else
	{
		float OrthoWidth  = CaptureComponent->OrthoWidth / 2.0f;
		float OrthoHeight = OrthoWidth / AspectRatio;
		ProjectionMatrix  = FReversedZOrthoMatrix(OrthoWidth, OrthoHeight, 0.5f / OrthoWidth, GNearClippingPlane);
	}

	FMatrix ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;

	for (UActorComponent* ActorComp : Actor->GetComponents())
	{
		UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp);
		if (!PrimComp || !PrimComp->IsCollisionEnabled()) continue;

		FBox      LocalBox      = PrimComp->CalcLocalBounds().GetBox();
		FTransform CompTransform = PrimComp->GetComponentTransform();

		FVector LocalCamPos = CompTransform.InverseTransformPosition(ViewLocation);
		FVector BoxCenter   = (LocalBox.Min + LocalBox.Max) * 0.5f;
		FVector LocalDir    = BoxCenter - LocalCamPos;

		int32 DepthAxis = 0;
		float MaxAbs = FMath::Abs(LocalDir.X);
		if (FMath::Abs(LocalDir.Y) > MaxAbs) { MaxAbs = FMath::Abs(LocalDir.Y); DepthAxis = 1; }
		if (FMath::Abs(LocalDir.Z) > MaxAbs) { DepthAxis = 2; }

		const float DepthCenter = (LocalBox.Min[DepthAxis] + LocalBox.Max[DepthAxis]) * 0.5f;

		for (int32 i = 0; i < 4; ++i)
		{
			const bool b0 = (i & 1) != 0;
			const bool b1 = (i & 2) != 0;

			FVector c;
			if (DepthAxis == 0)
				c = FVector(DepthCenter, b0 ? LocalBox.Max.Y : LocalBox.Min.Y, b1 ? LocalBox.Max.Z : LocalBox.Min.Z);
			else if (DepthAxis == 1)
				c = FVector(b0 ? LocalBox.Max.X : LocalBox.Min.X, DepthCenter, b1 ? LocalBox.Max.Z : LocalBox.Min.Z);
			else
				c = FVector(b0 ? LocalBox.Max.X : LocalBox.Min.X, b1 ? LocalBox.Max.Y : LocalBox.Min.Y, DepthCenter);

			FVector  WorldCorner = CompTransform.TransformPosition(c);
			FVector4 Projected   = ViewProjectionMatrix.TransformFVector4(FVector4(WorldCorner, 1.f));

			if (Projected.W > 0.0f)
			{
				float RHW = 1.0f / Projected.W;
				Result += FVector2D(
					(Projected.X * RHW + 1.0f) * (SizeX / 2.0f),
					(1.0f - Projected.Y * RHW) * (SizeY / 2.0f)
				);
			}
		}
	}

	if (Result.bIsValid)
	{
		Result.Min.X = FMath::Clamp(Result.Min.X, 0.0f, static_cast<float>(SizeX));
		Result.Min.Y = FMath::Clamp(Result.Min.Y, 0.0f, static_cast<float>(SizeY));
		Result.Max.X = FMath::Clamp(Result.Max.X, 0.0f, static_cast<float>(SizeX));
		Result.Max.Y = FMath::Clamp(Result.Max.Y, 0.0f, static_cast<float>(SizeY));
	}

	return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialization
// ─────────────────────────────────────────────────────────────────────────────

FString UBBoxDetectionComponent::SerializeBBoxes(const TMap<FString, FBox2D>& BBoxMap) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	for (const auto& Pair : BBoxMap)
	{
		TSharedRef<FJsonObject> Box = MakeShared<FJsonObject>();

		TSharedRef<FJsonObject> Min = MakeShared<FJsonObject>();
		Min->SetNumberField(TEXT("X"), Pair.Value.Min.X);
		Min->SetNumberField(TEXT("Y"), Pair.Value.Min.Y);
		Box->SetObjectField(TEXT("Min"), Min);

		TSharedRef<FJsonObject> Max = MakeShared<FJsonObject>();
		Max->SetNumberField(TEXT("X"), Pair.Value.Max.X);
		Max->SetNumberField(TEXT("Y"), Pair.Value.Max.Y);
		Box->SetObjectField(TEXT("Max"), Max);

		Box->SetBoolField(TEXT("bIsValid"), Pair.Value.bIsValid);
		Root->SetObjectField(Pair.Key, Box);
	}

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}
