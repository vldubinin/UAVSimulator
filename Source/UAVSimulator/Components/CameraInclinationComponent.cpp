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
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void UCameraInclinationComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner)
		CaptureComp = Owner->FindComponentByClass<USceneCaptureComponent2D>();

	if (!CaptureComp)
		UE_LOG(LogUAV, Warning, TEXT("CameraInclinationComponent: USceneCaptureComponent2D not found on %s — pitch will not be reported."),
			Owner ? *Owner->GetName() : TEXT("Unknown"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UCameraInclinationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!CaptureComp) return;

	LatestPitchDeg  = CaptureComp->GetComponentRotation().Pitch;
	LatestTimestamp = GetWorld()->GetTimeSeconds();
	bHasData        = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// IUAVSensorInterface — called on game thread by SensorBusComponent
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
