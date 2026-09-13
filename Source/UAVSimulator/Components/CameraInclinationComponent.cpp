#include "CameraInclinationComponent.h"
#include "UAVSimulator/UAVSimulator.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UCameraInclinationComponent::UCameraInclinationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Життєвий цикл
// ─────────────────────────────────────────────────────────────────────────────

void UCameraInclinationComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner)
		CaptureComp = Owner->FindComponentByClass<USceneCaptureComponent2D>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Тік
// ─────────────────────────────────────────────────────────────────────────────

void UCameraInclinationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSensorEnabled || !CaptureComp) return;

	LatestPitchDeg  = CaptureComp->GetComponentRotation().Pitch;
	LatestTimestamp = GetWorld()->GetTimeSeconds();
	bHasData        = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — викликається в ігровому потоці компонентом SensorBusComponent
// ─────────────────────────────────────────────────────────────────────────────

bool UCameraInclinationComponent::GetLatestFrame(FSensorFrame& OutFrame)
{
	if (!bHasData) return false;

	TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetNumberField(TEXT("pitch_deg"), LatestPitchDeg);

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
