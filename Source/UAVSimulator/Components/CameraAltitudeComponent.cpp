#include "CameraAltitudeComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UCameraAltitudeComponent::UCameraAltitudeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Життєвий цикл
// ─────────────────────────────────────────────────────────────────────────────

void UCameraAltitudeComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner)
		CaptureComp = Owner->FindComponentByClass<USceneCaptureComponent2D>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Тік
// ─────────────────────────────────────────────────────────────────────────────

void UCameraAltitudeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled || !CaptureComp) return;

	// Unreal використовує см; конвертуємо в метри
	LatestAltitudeMeters = CaptureComp->GetComponentLocation().Z * 0.01f;
	LatestTimestamp      = GetWorld()->GetTimeSeconds();
	bHasData             = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — викликається в ігровому потоці компонентом SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool UCameraAltitudeComponent::GetLatestFrame(FSensorFrame& OutFrame)
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
