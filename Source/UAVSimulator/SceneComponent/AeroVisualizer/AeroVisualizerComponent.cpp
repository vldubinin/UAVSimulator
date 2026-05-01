#include "AeroVisualizerComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "UAVSimulator/Components/FlightDynamicsComponent.h"
#include "UAVSimulator/SceneComponent/AerodynamicSurface/AerodynamicSurfaceSC.h"

UAeroVisualizerComponent::UAeroVisualizerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UAeroVisualizerComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	FlightDynamicsComp = Owner->FindComponentByClass<UFlightDynamicsComponent>();
	if (FlightDynamicsComp)
	{
		AddTickPrerequisiteComponent(FlightDynamicsComp);
	}

	if (!FlowVisualizerSystem) return;

	TArray<UAerodynamicSurfaceSC*> Surfaces;
	Owner->GetComponents<UAerodynamicSurfaceSC>(Surfaces);

	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		const FString Name = Surface->GetName();
		if (!Name.Contains(TEXT("Wing")) && !Name.Contains(TEXT("TailHorizontal"))) continue;

		UNiagaraComponent* NiagaraComp = NewObject<UNiagaraComponent>(Owner);
		NiagaraComp->SetAsset(FlowVisualizerSystem);
		NiagaraComp->SetupAttachment(Surface);

		float SpanCm = 0.0f;
		for (const FAerodynamicSurfaceStructure& Form : Surface->SurfaceForm)
		{
			SpanCm += FMath::Abs(Form.Offset.Y);
		}
		if (Surface->Mirror) SpanCm *= 2.0f;

		if (Name.Contains(TEXT("TailHorizontal")))
		{
			NiagaraComp->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));
			NiagaraComp->SetFloatParameter(FName("SurfaceSpan"), SpanCm * 1.5f);
			NiagaraComp->SetFloatParameter(FName("ProbeHeight"), 150.0f);
		}
		else
		{
			NiagaraComp->SetFloatParameter(FName("SurfaceSpan"), SpanCm);
			NiagaraComp->SetFloatParameter(FName("ProbeHeight"), 2.0f);
		}

		NiagaraComp->bAutoActivate = false;
		NiagaraComp->RegisterComponent();
		ActiveFlowVisualizers.Add(NiagaraComp);
	}
}

void UAeroVisualizerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateNiagaraWakeData();
}

void UAeroVisualizerComponent::UpdateNiagaraWakeData()
{
	if (!FlightDynamicsComp || ActiveFlowVisualizers.Num() == 0) return;

	const TArray<TArray<FTrailingVortexNode>>& WakeLines = FlightDynamicsComp->GetVortexWakeLines();

	TArray<FVector> FlatWakePositions;
	TArray<float>   FlatWakeGammas;

	int32 TotalNodes = 0;
	for (const TArray<FTrailingVortexNode>& Line : WakeLines) TotalNodes += Line.Num() + 1;
	FlatWakePositions.Reserve(TotalNodes);
	FlatWakeGammas.Reserve(TotalNodes);

	for (const TArray<FTrailingVortexNode>& Line : WakeLines)
	{
		for (const FTrailingVortexNode& Node : Line)
		{
			FlatWakePositions.Add(Node.Position);
			FlatWakeGammas.Add(Node.Gamma);
		}
		if (Line.Num() > 0)
		{
			// Sentinel node signals end-of-line to the Niagara ribbon
			FlatWakePositions.Add(Line.Last().Position + FVector(0.0f, 0.0f, 100000.0f));
			FlatWakeGammas.Add(0.0f);
		}
	}

	for (UNiagaraComponent* Visualizer : ActiveFlowVisualizers)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Visualizer, FName("WakePositions"), FlatWakePositions);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(Visualizer,  FName("WakeGammas"),    FlatWakeGammas);
	}
}
