// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicalAirplane.h"



// Sets default values
APhysicalAirplane::APhysicalAirplane()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	CVCaptureComponent = nullptr;
}

void APhysicalAirplane::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	CalculateParameters();
	ControlState = ControlInputState();
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces) {
		Surface->OnConstruction(CenterOfMassInWorld, ControlSurfaces);
	}
}

// Called when the game starts or when spawned
void APhysicalAirplane::BeginPlay()
{
	Super::BeginPlay();

	CVCaptureComponent = FindComponentByClass<USceneCaptureComponent2D>();

	if (CVCaptureComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("CV: Camera Found!"));

		// 2. Створюємо динамічну текстуру для запису (Render Target)
		// Це краще робити в коді, щоб гарантувати правильний формат пікселів для OpenCV
		CVRenderTarget = NewObject<UTextureRenderTarget2D>();
		CVRenderTarget->InitCustomFormat(CVWidth, CVHeight, PF_B8G8R8A8, false); // Формат BGRA 8bit
		CVRenderTarget->UpdateResourceImmediate(false);

		// 3. Підключаємо текстуру до вашої камери
		CVCaptureComponent->TextureTarget = CVRenderTarget;

		// 4. Налаштовуємо режим захоплення (LDR для коректних кольорів в OpenCV)
		CVCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

		// Опціонально: вимкнути захоплення кожного кадру, якщо хочете контролювати це вручну
		// CVCaptureComponent->bCaptureEveryFrame = true; 

		OutputTexture = UTexture2D::CreateTransient(CVWidth, CVHeight, PF_B8G8R8A8);
		OutputTexture->UpdateResource();

		// Ініціалізація регіону оновлення (весь екран)
		VideoUpdateTextureRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, CVWidth, CVHeight);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("CV: SceneCaptureComponent2D NOT FOUND in Blueprint! Please add it."));
	}

	ControlState = ControlInputState();
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), DebugSimulatorSpeed);

	GetComponents<UControlSurfaceSC>(ControlSurfaces);
	GetComponents<UAerodynamicSurfaceSC>(Surfaces);
	for (UAerodynamicSurfaceSC* Surface : Surfaces) {
		Surface->OnConstruction(CenterOfMassInWorld, ControlSurfaces);
	}
}

void APhysicalAirplane::ProcessCameraFrame()
{
	if (!CVRenderTarget->GameThread_GetRenderTargetResource()) return;

	FTextureRenderTargetResource* RTResource = CVRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> ColorBuffer;

	// Читаємо пікселі
	RTResource->ReadPixels(ColorBuffer);

	if (ColorBuffer.Num() > 0)
	{

		// 2. Створюємо cv::Mat з буфера Unreal (BGRA)
	    // Важливо: це лише обгортка над даними, копіювання тут не відбуваєть
		cv::Mat FrameBGRA(CVHeight, CVWidth, CV_8UC4, ColorBuffer.GetData());
		cv::Mat CameraFrame;

		// 2. Відзеркалення по горизонталі
		// 1  = Горизонтально (вісь Y)
		// 0  = Вертикально (вісь X)
		// -1 = Обидві осі
		cv::flip(FrameBGRA, CameraFrame, 1);

		// 3. Конвертуємо в BGR для малювання (якщо функції OpenCV не підтримують 4 канали)
		// АБО малюємо прямо по BGRA, якщо це прості фігури

		// Варіант А: Малюємо прямо по BGRA (швидше)
		// Червоний колір в BGRA це (0, 0, 255, 255)
		cv::circle(CameraFrame, cv::Point(CVWidth / 2, CVHeight / 2), 50, cv::Scalar(0, 0, 255, 255), 4);

		// Варіант Б: Якщо потрібна складна обробка в BGR
		/*
		cv::Mat FrameBGR;
		cv::cvtColor(FrameBGRA, FrameBGR, cv::COLOR_BGRA2BGR);
		// ... обробка FrameBGR ...
		// Потім конвертуємо назад, щоб Unreal зрозумів
		cv::cvtColor(FrameBGR, FrameBGRA, cv::COLOR_BGR2BGRA);
		*/

		// 4. Оновлюємо нашу OutputTexture (CPU -> GPU)
		// Нам потрібно отримати доступ до даних текстури

		// Блокуємо дані текстури для запису
		FTexture2DMipMap& Mip = OutputTexture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);

		// Копіюємо пам'ять з OpenCV Mat в Текстуру
		// FrameBGRA.data - це вказівник на початок даних зображення
		FMemory::Memcpy(Data, CameraFrame.data, CameraFrame.total() * CameraFrame.elemSize());

		// Розблокуємо
		Mip.BulkData.Unlock();

		// Кажемо рушію оновити ресурс
		OutputTexture->UpdateResource();
	}
}

// Called every frame
void APhysicalAirplane::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UE_LOG(LogTemp, Warning, TEXT("### TICK ###"));

	if (CVCaptureComponent && CVRenderTarget)
	{
		//ToDo
		/*ControlState = */ProcessCameraFrame();
	}

	CalculateParameters(); 

	UStaticMeshComponent* StaticMeshComponent = this->FindComponentByClass<UStaticMeshComponent>();
	if (StaticMeshComponent && StaticMeshComponent->IsSimulatingPhysics())
	{
		/*DrawDebugDirectionalArrow(
			GetWorld(),
			CenterOfMassInWorld,
			CenterOfMassInWorld + LinearVelocity * 200,
			25.0f, FColor::Red, false, -1.f, 0, 5.0f);

		DrawDebugDirectionalArrow(
			GetWorld(),
			CenterOfMassInWorld,
			CenterOfMassInWorld + AirflowDirection * 200,
			25.0f, FColor::Cyan, false, -1.f, 0, 5.0f);
		
		DrawDebugCrosshairs(GetWorld(), CenterOfMassInWorld, FRotator::ZeroRotator, 250, FColor::Red, false, -1, 0);*/

		AerodynamicForce TotalAerodynamicForce;
		for (UAerodynamicSurfaceSC* Surface : Surfaces) {
			AerodynamicForce SurfaceAerodynamicForce = Surface->CalculateForcesOnSurface(CenterOfMassInWorld, LinearVelocity, AngularVelocity, AirflowDirection, ControlState);
			TotalAerodynamicForce.PositionalForce += SurfaceAerodynamicForce.PositionalForce;
			TotalAerodynamicForce.RotationalForce += SurfaceAerodynamicForce.RotationalForce;
		}
	
		StaticMeshComponent->AddForce(TotalAerodynamicForce.PositionalForce);
		StaticMeshComponent->AddTorqueInRadians(TotalAerodynamicForce.RotationalForce);

		if (ThrottlePercent > 0.0f) {
			float MaxThrust = 1500000.f;
			FVector ThrustDirection = GetActorForwardVector();
			FVector ThrustForce = ThrustDirection * MaxThrust * ThrottlePercent;
			StaticMeshComponent->AddForce(ThrustForce);
		}

		FVector CurrentLocation = GetActorLocation();

		// 2. Format the log string
		FString LogString = FString::Printf(
			TEXT("%s Location: %s"),
			*GetName(),
			*CurrentLocation.ToString()
		);

		// 3. Log the message to the Unreal Output Log (Ctrl+Shift+Z to view)
		// We use the custom LogPositionLogger category defined in the header.
		UE_LOG(LogTemp, Log, TEXT("%s"), *LogString);

	}
	ControlState = ControlInputState();
}

void  APhysicalAirplane::CalculateParameters()
{
	//Calculate LinearVelocity
	//Calculate AngularVelocity
	//Calculate CenterOfMass
	UStaticMeshComponent* StaticMeshComponent = this->FindComponentByClass<UStaticMeshComponent>();
	if (StaticMeshComponent && StaticMeshComponent->IsSimulatingPhysics())
	{
		LinearVelocity = StaticMeshComponent->GetPhysicsLinearVelocity();
		AngularVelocity = StaticMeshComponent->GetPhysicsAngularVelocityInRadians();
		CenterOfMassInWorld = StaticMeshComponent->GetCenterOfMass();
	}
	else {
		AngularVelocity = FVector();
		CenterOfMassInWorld = FVector();
	}

	//Calculate AirflowDirection
	const FVector ActorVelocity = GetVelocity();
	if (ActorVelocity.IsNearlyZero())
	{
		AirflowDirection = FVector();
	}
	else {
		AirflowDirection = -ActorVelocity.GetSafeNormal();
	}
}

void APhysicalAirplane::UpdateAileronControl(float LeftAileronAngleValue, float RightAileronAngleValue)
{
	ControlState.LeftAileronAngle = LeftAileronAngleValue;
	ControlState.RightAileronAngle = RightAileronAngleValue;
}

void APhysicalAirplane::UpdateElevatorControl(float LeftElevatorAngleValue, float RightElevatorAngleValue)
{
	ControlState.LeftElevatorAngle = LeftElevatorAngleValue;
	ControlState.RightElevatorAngle = RightElevatorAngleValue;
}

void APhysicalAirplane::UpdateRudderControl(float RudderAngleValue)
{
	ControlState.RudderAngle = RudderAngleValue;
}



void APhysicalAirplane::GenerateAerodynamicPhysicalConfigutation(UObject* ContextObject)
{
	AerodynamicPhysicalCalculationUtil::GenerateAerodynamicPhysicalConfigutation(ContextObject, Surfaces);
}