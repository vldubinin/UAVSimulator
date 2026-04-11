// Fill out your copyright notice in the Description page of Project Settings.

#include "PhysicalAirplane.h"
#include "UAVSimulator/UAVSimulator.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"

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
