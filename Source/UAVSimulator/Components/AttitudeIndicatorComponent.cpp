#include "AttitudeIndicatorComponent.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UAttitudeIndicatorComponent::UAttitudeIndicatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Тік
// ─────────────────────────────────────────────────────────────────────────────

void UAttitudeIndicatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled) return;

	AActor* Owner = GetOwner();
	if (!Owner) return;

	const FRotator Rot = Owner->GetActorRotation();
	LatestRollDeg  = Rot.Roll;
	LatestPitchDeg = Rot.Pitch;
	LatestYawDeg   = Rot.Yaw;

	// Кутова швидкість береться з фізики mesh (той самий шлях, що в AttitudeControlComponent).
	// Якщо фізика не симулюється — повертається нуль.
	if (UStaticMeshComponent* Mesh = Owner->FindComponentByClass<UStaticMeshComponent>())
	{
		LatestAngularRateDps = Mesh->GetPhysicsAngularVelocityInDegrees();
	}
	else
	{
		LatestAngularRateDps = FVector::ZeroVector;
	}

	LatestTimestamp = GetWorld()->GetTimeSeconds();
	bHasData        = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — викликається в ігровому потоці компонентом SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool UAttitudeIndicatorComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bHasData) return false;

	TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetNumberField(TEXT("roll_deg"),  LatestRollDeg);
	JsonObj->SetNumberField(TEXT("pitch_deg"), LatestPitchDeg);
	JsonObj->SetNumberField(TEXT("yaw_deg"),   LatestYawDeg);
	JsonObj->SetNumberField(TEXT("roll_rate_dps"),  LatestAngularRateDps.X);
	JsonObj->SetNumberField(TEXT("pitch_rate_dps"), LatestAngularRateDps.Y);
	JsonObj->SetNumberField(TEXT("yaw_rate_dps"),   LatestAngularRateDps.Z);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, Writer);

	FTCHARToUTF8 Utf8(*JsonString);

	OutFrame.Topic     = GetSensorTopic();
	OutFrame.Timestamp = LatestTimestamp;
	OutFrame.Payload.Reset();
	OutFrame.Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return true;
}
