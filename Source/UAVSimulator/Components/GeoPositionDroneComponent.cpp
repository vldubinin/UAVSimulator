#include "GeoPositionDroneComponent.h"

#include "CesiumGeoreference.h"

#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UGeoPositionDroneComponent::UGeoPositionDroneComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGeoPositionDroneComponent::BeginPlay()
{
	Super::BeginPlay();

	Georeference = ACesiumGeoreference::GetDefaultGeoreference(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UGeoPositionDroneComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled) return;

	AActor* Owner = GetOwner();
	if (!Owner || !Georeference) return;

	const FVector LocalPositionCm = Georeference->GetActorTransform().InverseTransformPosition(Owner->GetActorLocation());
	LatestLongitudeLatitudeHeight = Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(LocalPositionCm);
	LatestTimestamp                = GetWorld()->GetTimeSeconds();
	bHasData                       = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — called on game thread by SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool UGeoPositionDroneComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bHasData) return false;

	TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetNumberField(TEXT("latitude"),   LatestLongitudeLatitudeHeight.Y);
	JsonObj->SetNumberField(TEXT("longitude"),  LatestLongitudeLatitudeHeight.X);
	JsonObj->SetNumberField(TEXT("altitude_m"), LatestLongitudeLatitudeHeight.Z);

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
