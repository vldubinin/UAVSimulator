// Fill out your copyright notice in the Description page of Project Settings.

#include "PhysicalAirplane.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

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

			const FVector SurfaceCenter = Surface->GetComponentLocation();
			const FVector RightDir      = GetActorRightVector();

			FBoundVortex BV;
			BV.StartPoint = SurfaceCenter - RightDir * (SpanCm / 2.0f);
			BV.EndPoint   = SurfaceCenter + RightDir * (SpanCm / 2.0f);
			BV.Gamma      = Gamma;
			CurrentBoundVortices.Add(BV);
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

		// З кореня скидається -Gamma, з кінцівки скидається +Gamma
		TryAddNode(i * 2,     BV.StartPoint, -BV.Gamma);
		TryAddNode(i * 2 + 1, BV.EndPoint,    BV.Gamma);
	}
}

void APhysicalAirplane::SendWakeDataToNiagara()
{
	if (!FlowVisualizer) return;

	TArray<FVector> FlatWakePositions;
	TArray<float>   FlatWakeGammas;

	int32 TotalNodes = 0;
	for (const auto& Line : VortexWakeLines) TotalNodes += Line.Num();
	FlatWakePositions.Reserve(TotalNodes);
	FlatWakeGammas.Reserve(TotalNodes);

	for (const auto& Line : VortexWakeLines)
	{
		for (const FTrailingVortexNode& Node : Line)
		{
			FlatWakePositions.Add(Node.Position);
			FlatWakeGammas.Add(Node.Gamma);
		}
	}

	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(FlowVisualizer, FName("WakePositions"), FlatWakePositions);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(FlowVisualizer, FName("WakeGammas"),    FlatWakeGammas);
}
