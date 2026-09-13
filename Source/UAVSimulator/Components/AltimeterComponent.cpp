#include "AltimeterComponent.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UAltimeterComponent::UAltimeterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Тік
// ─────────────────────────────────────────────────────────────────────────────

void UAltimeterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled) return;

	AActor* Owner = GetOwner();
	if (!Owner) return;

	// Unreal використовує см; конвертуємо в метри
	LatestAltitudeMeters = Owner->GetActorLocation().Z * 0.01f;
	LatestTimestamp      = GetWorld()->GetTimeSeconds();
	bHasData             = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — викликається в ігровому потоці компонентом SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool UAltimeterComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bHasData) return false;

	TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetNumberField(TEXT("altitude_m"), LatestAltitudeMeters);

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
