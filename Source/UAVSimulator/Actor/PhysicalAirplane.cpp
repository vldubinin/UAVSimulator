// Fill out your copyright notice in the Description page of Project Settings.

#include "PhysicalAirplane.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraSystem.h"

APhysicalAirplane::APhysicalAirplane()
{
	PrimaryActorTick.bCanEverTick = true;

	// Субкомпоненти реєструються тут; реальна ініціалізація відбувається у BeginPlay
	CameraComp   = CreateDefaultSubobject<UUAVCameraComponent>(TEXT("CameraComp"));
	PhysicsState = CreateDefaultSubobject<UUAVPhysicsStateComponent>(TEXT("PhysicsState"));
}

void APhysicalAirplane::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Скидаємо стан керування щоразу при перебудові актора в редакторі
	ControlState = FControlInputState();
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);

	// Центр мас потрібен для розрахунку відстані до центру тиску кожної підсекції
	UStaticMeshComponent* Mesh = FindComponentByClass<UStaticMeshComponent>();
	FVector CoM = Mesh ? Mesh->GetCenterOfMass() : FVector::ZeroVector;

	TArray<UControlSurfaceSC*> AllControlSurfaces;
	GetComponents<UControlSurfaceSC>(AllControlSurfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		// Передаємо повний список керуючих поверхонь — кожна поверхня сама знайде свою
		Surface->OnConstruction(CoM, AllControlSurfaces);
	}
}

void APhysicalAirplane::BeginPlay()
{
	Super::BeginPlay();

	// Масштабуємо час симуляції відповідно до налагоджувального множника
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);

	ControlState = FControlInputState();
	GetComponents<UControlSurfaceSC>(ControlSurfaces);
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);

	// Повторна ініціалізація поверхонь потрібна бо у BeginPlay центр мас вже враховує фізику
	UStaticMeshComponent* Mesh = FindComponentByClass<UStaticMeshComponent>();
	FVector CoM = Mesh ? Mesh->GetCenterOfMass() : FVector::ZeroVector;
	for (UAerodynamicSurfaceSC* Surface : Surfaces)
	{
		Surface->OnConstruction(CoM, ControlSurfaces);
	}

	if (FlowVisualizerSystem)
	{
		for (UAerodynamicSurfaceSC* Surface : Surfaces)
		{
			if (!Surface->GetName().Contains(TEXT("Wing"))) continue;

			UNiagaraComponent* NiagaraComp = NewObject<UNiagaraComponent>(this);
			NiagaraComp->SetAsset(FlowVisualizerSystem);
			NiagaraComp->SetupAttachment(Surface);
			NiagaraComp->RegisterComponent();

			float SpanCm = 0.0f;
			for (const auto& Form : Surface->SurfaceForm) { SpanCm += FMath::Abs(Form.Offset.Y); }
			if (Surface->Mirror) SpanCm *= 2.0f;

			NiagaraComp->SetFloatParameter(FName("SurfaceSpan"), SpanCm);
			ActiveFlowVisualizers.Add(NiagaraComp);
		}
	}
}

void APhysicalAirplane::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UE_LOG(LogUAV, Verbose, TEXT("### TICK ###"));

	// Захоплення та обробка кадру бортової камери (OpenCV)
	if (CameraComp) CameraComp->ProcessFrame();

	// Оновлюємо кеш фізичного стану перед зчитуванням швидкості та центру мас
	PhysicsState->Update();

	UStaticMeshComponent* Mesh = FindComponentByClass<UStaticMeshComponent>();
	if (Mesh && Mesh->IsSimulatingPhysics())
	{
		CurrentBoundVortices.Empty();

		const float SpeedMs = PhysicsState->GetLinearVelocity().Size() / 100.0f;

		// Накопичуємо сумарну аеродинамічну силу від усіх поверхонь
		FAerodynamicForce TotalForce;
		for (UAerodynamicSurfaceSC* Surface : Surfaces)
		{
			FAerodynamicForce SurfaceForce = Surface->CalculateForcesOnSurface(
				PhysicsState->GetCenterOfMass(),
				PhysicsState->GetLinearVelocity(),
				PhysicsState->GetAngularVelocity(),
				PhysicsState->GetAirflowDirection(),
				ControlState);
			TotalForce.PositionalForce += SurfaceForce.PositionalForce;
			TotalForce.RotationalForce += SurfaceForce.RotationalForce;

			// Розраховуємо розмах поверхні з SurfaceForm для геометрії Bound Vortex
			float SpanCm = 0.0f;
			for (const auto& Form : Surface->SurfaceForm)
			{
				SpanCm += FMath::Abs(Form.Offset.Y);
			}
			if (Surface->Mirror) SpanCm *= 2.0f;
			const float SpanM = SpanCm / 100.0f;

			// Кутта-Жуковський у зворотньому напрямку: Γ = F / (ρ * V * b)
			const float ForceN = SurfaceForce.PositionalForce.Size() / 100.0f;
			const float Gamma  = (SpeedMs > 0.1f && SpanM > 0.01f)
				? ForceN / (AirDensity * SpeedMs * SpanM)
				: 0.0f;

			UE_LOG(LogTemp, Warning, TEXT("Surface: %s | ForceN: %f | SpeedMs: %f | SpanM: %f | Calc Gamma: %f"),
				*Surface->GetName(), ForceN, SpeedMs, SpanM, Gamma);

			const FVector SurfaceCenter = Surface->GetComponentLocation();
			const FVector RightDir      = Surface->GetRightVector();

			// Еліптичний розподіл циркуляції по розмаху (дискретна несуча лінія)
			const float GammaMax  = Gamma * (4.0f / UE_PI);
			const int32 NumSegments = FMath::Clamp(FMath::RoundToInt(SpanM / 0.5f), 2, 20);
			const float SegmentLen  = SpanCm / NumSegments;
			for (int32 j = 0; j < NumSegments; j++)
			{
				const float LocalY      = -SpanCm / 2.0f + SegmentLen * j;
				const float SegmentCenterY = LocalY + SegmentLen / 2.0f;
				const float HalfSpan    = SpanCm / 2.0f;
				const float LocalGamma  = (HalfSpan > 0.0f)
					? GammaMax * FMath::Sqrt(FMath::Max(0.0f, 1.0f - FMath::Pow(SegmentCenterY / HalfSpan, 2.0f)))
					: 0.0f;

				FBoundVortex BV;
				BV.StartPoint = SurfaceCenter + RightDir * LocalY;
				BV.EndPoint   = SurfaceCenter + RightDir * (LocalY + SegmentLen);
				BV.Gamma      = LocalGamma;
				CurrentBoundVortices.Add(BV);
			}
		}

		// Застосовуємо сумарну підйомну силу та момент до rigid body
		Mesh->AddForce(TotalForce.PositionalForce);
		Mesh->AddTorqueInRadians(TotalForce.RotationalForce);

		// Тяга двигуна: максимум 1 500 000 kg·cm/s² (≈ 15 000 N), масштабована дроселем
		if (ThrottlePercent > 0.0f)
		{
			const float MaxThrust = 1500000.f;
			Mesh->AddForce(GetActorForwardVector() * MaxThrust * ThrottlePercent);
		}

		UE_LOG(LogUAV, Log, TEXT("%s Location: %s"), *GetName(), *GetActorLocation().ToString());
	}

	UpdateVortexWake();
	SendWakeDataToNiagara();

	// Скидаємо стан керування після застосування — нові значення надійдуть наступного тіку
	ControlState = FControlInputState();
}

void APhysicalAirplane::UpdateAileronControl(float LeftAileronAngleValue, float RightAileronAngleValue)
{
	// Зберігаємо кути елеронів; вони будуть застосовані при наступному розрахунку сил
	ControlState.LeftAileronAngle  = LeftAileronAngleValue;
	ControlState.RightAileronAngle = RightAileronAngleValue;
}

void APhysicalAirplane::UpdateElevatorControl(float LeftElevatorAngleValue, float RightElevatorAngleValue)
{
	// Зберігаємо кути рулів висоти
	ControlState.LeftElevatorAngle  = LeftElevatorAngleValue;
	ControlState.RightElevatorAngle = RightElevatorAngleValue;
}

void APhysicalAirplane::UpdateRudderControl(float RudderAngleValue)
{
	// Зберігаємо кут руля напрямку
	ControlState.RudderAngle = RudderAngleValue;
}

void APhysicalAirplane::GenerateAerodynamicPhysicalConfigutation()
{
	// Делегуємо генерацію полярів утиліті, яка запускає Python/OpenVSP/XFoil pipeline
	AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(Surfaces);
}

UTexture2D* APhysicalAirplane::GetCameraOutputTexture() const
{
	// Повертаємо nullptr якщо компонент камери не було створено
	return CameraComp ? CameraComp->OutputTexture : nullptr;
}

void APhysicalAirplane::UpdateVortexWake()
{
	// На кожен приєднаний вихор маємо 2 лінії сліду (корінь і кінцівка)
	if (VortexWakeLines.Num() != CurrentBoundVortices.Num() * 2)
	{
		VortexWakeLines.SetNum(CurrentBoundVortices.Num() * 2);
	}

	for (int32 i = 0; i < CurrentBoundVortices.Num(); i++)
	{
		const FBoundVortex& BV = CurrentBoundVortices[i];

		auto TryAddNode = [&](int32 LineIndex, FVector Pos, float NodeGamma)
		{
			TArray<FTrailingVortexNode>& Line = VortexWakeLines[LineIndex];
			if (Line.Num() == 0 || FVector::Distance(Line.Last().Position, Pos) >= MinWakeDistance)
			{
				FTrailingVortexNode Node;
				Node.Position = Pos;
				Node.Gamma    = NodeGamma;
				Line.Add(Node);
				if (Line.Num() > MaxWakeLength)
				{
					Line.RemoveAt(0);
				}
			}
		};

		// З кореня скидається +Gamma, з кінцівки скидається -Gamma (правило правої руки: сегмент іде Root→Tip)
		TryAddNode(i * 2,     BV.StartPoint,  BV.Gamma);
		TryAddNode(i * 2 + 1, BV.EndPoint,   -BV.Gamma);
	}
}

void APhysicalAirplane::SendWakeDataToNiagara()
{
	if (ActiveFlowVisualizers.Num() == 0) return;

	TArray<FVector> FlatWakePositions;
	TArray<float>   FlatWakeGammas;

	int32 TotalNodes = 0;
	for (const auto& Line : VortexWakeLines) TotalNodes += Line.Num() + 1;
	FlatWakePositions.Reserve(TotalNodes);
	FlatWakeGammas.Reserve(TotalNodes);

	for (const auto& Line : VortexWakeLines)
	{
		for (const FTrailingVortexNode& Node : Line)
		{
			FlatWakePositions.Add(Node.Position);
			FlatWakeGammas.Add(Node.Gamma);
		}
		if (Line.Num() > 0)
		{
			FlatWakePositions.Add(Line.Last().Position + FVector(0, 0, 100000.0f));
			FlatWakeGammas.Add(0.0f);
		}
	}

	for (UNiagaraComponent* Visualizer : ActiveFlowVisualizers)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Visualizer, FName("WakePositions"), FlatWakePositions);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(Visualizer, FName("WakeGammas"),    FlatWakeGammas);
	}
}
