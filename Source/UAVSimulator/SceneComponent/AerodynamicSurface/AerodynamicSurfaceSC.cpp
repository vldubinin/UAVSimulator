// Fill out your copyright notice in the Description page of Project Settings.


#include "AerodynamicSurfaceSC.h"
#include "UAVSimulator/UAVSimulator.h"
#include "NiagaraComponent.h"



// Sets default values for this component's properties
UAerodynamicSurfaceSC::UAerodynamicSurfaceSC()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}


void UAerodynamicSurfaceSC::OnConstruction(FVector CenterOfMass, TArray<UControlSurfaceSC*> ControlSur)
{
	if (!Enable) {
		return;
	}
	ControlSurfaces = ControlSur;

	// Знищуємо попередні підповерхні перед повторною побудовою (наприклад, при зміні конфігу в редакторі)
	DestroySubsurfaces();

	// Будуємо основну сторону (+1), та дзеркальну (-1) якщо поверхня симетрична
	BuildSubsurfaces(CenterOfMass, 1);
	if (Mirror) {
		BuildSubsurfaces(CenterOfMass, -1);
	}
}

AerodynamicForce UAerodynamicSurfaceSC::CalculateForcesOnSurface(FVector CenterOfMass, FVector LinearVelocity, FVector AngularVelocity, FVector AirflowDirection, ControlInputState ControlState, bool bVisualizeForces)
{
	// Підсумовуємо позиційні сили та моменти від усіх підсекцій
	AerodynamicForce TotalAerodynamicForceForAllSubSurfaces;
	for (USubAerodynamicSurfaceSC* SubSurface : SubSurfaces)
	{
		AerodynamicForce SubSurfaceForces = SubSurface->CalculateForcesOnSubSurface(LinearVelocity, AngularVelocity, CenterOfMass, AirflowDirection, ControlState, bVisualizeForces);
		TotalAerodynamicForceForAllSubSurfaces.PositionalForce += SubSurfaceForces.PositionalForce;
		TotalAerodynamicForceForAllSubSurfaces.RotationalForce += SubSurfaceForces.RotationalForce;
	}
	return TotalAerodynamicForceForAllSubSurfaces;
}

void UAerodynamicSurfaceSC::SetNiagaraActive(bool bActive)
{
	for (USceneComponent* Child : GetAttachChildren())
	{
		if (UNiagaraComponent* NiagaraComp = Cast<UNiagaraComponent>(Child))
		{
			if (bActive)
			{
				NiagaraComp->Activate();
			}
			else
			{
				NiagaraComp->Deactivate();
			}
		}
	}
}

void UAerodynamicSurfaceSC::DestroySubsurfaces()
{
	// Явно знищуємо компоненти щоб уникнути витоку пам'яті при повторній ініціалізації
	for (USubAerodynamicSurfaceSC* SubSurface : SubSurfaces)
	{
		if (SubSurface) SubSurface->DestroyComponent();
	}
	SubSurfaces.Empty();
}

void UAerodynamicSurfaceSC::BuildSubsurfaces(FVector CenterOfMass, int32 Direction)
{
	bool IsMirror = Direction < 0;

	// Нормалізуємо профіль (інверсія X) для правильного орієнтування в Unreal
	TArray<FAirfoilPointData> Points = AerodynamicUtil::NormalizePoints(GetPoints());
	if (Points.Num() == 0) {
		return;  // Таблиця профілю не налаштована
	}

	FChord ProfileChord = AerodynamicUtil::FindChord(Points);
	if (FMath::IsNearlyZero(ProfileChord.Length))
	{
		return;  // Профіль вироджений — хорда має нульову довжину
	}

	if (SurfaceForm.Num() < 2)
	{
		return;  // Для побудови хоча б однієї секції потрібно мінімум 2 записи у SurfaceForm
	}

	// GlobalOffset накопичує зміщення кожної секції вздовж розмаху
	FVector GlobalOffset = FVector::ZeroVector;
	for (int32 i = 0; i < SurfaceForm.Num() - 1; i++)
	{
		const FAerodynamicSurfaceStructure& StartConfig = SurfaceForm[i];
		const FAerodynamicSurfaceStructure& EndConfig = SurfaceForm[i + 1];

		// Генеруємо 3D-профілі для початку та кінця секції з поточними розмірами хорди
		TArray<FVector> Start3DProfile = AerodynamicUtil::ConvertTo3DPoints(Points, ProfileChord.Length, StartConfig.ChordSize, GlobalOffset);
		// Зміщення по Y множиться на Direction щоб дзеркально відобразити по розмаху
		GlobalOffset += FVector(EndConfig.Offset.X, EndConfig.Offset.Y * Direction, EndConfig.Offset.Z);
		TArray<FVector> End3DProfile = AerodynamicUtil::ConvertTo3DPoints(Points, ProfileChord.Length, EndConfig.ChordSize, GlobalOffset);

		FName ComponentName = FName(*FString::Printf(TEXT("Sub_%s__%d_dir_%d"), *this->GetName(), i, Direction));
		USubAerodynamicSurfaceSC* SubAerodynamicSurface = NewObject<USubAerodynamicSurfaceSC>(this, ComponentName);
		if (SubAerodynamicSurface)
		{
			SubAerodynamicSurface->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			SubAerodynamicSurface->RegisterComponent();

			// Знаходимо відповідний компонент-закрилок за типом і дзеркальністю
			UControlSurfaceSC* ControlSurface = FindControlSurface(StartConfig.FlapType, IsMirror);

			// AerodynamicTable може бути nullptr — підповерхня все одно створюється для відображення
			SubAerodynamicSurface->InitComponent(Start3DProfile, End3DProfile, ComponentName, AerodynamicCenterOffsetPercent, CenterOfMass, StartConfig.MinFlapAngle, StartConfig.MaxFlapAngle, StartConfig.StartFlapPosition, StartConfig.EndFlapPosition, StartConfig.AerodynamicTable, IsMirror, StartConfig.FlapType, ControlSurface);
			SubSurfaces.Add(SubAerodynamicSurface);
		}
	}
}

TArray<FAirfoilPointData> UAerodynamicSurfaceSC::GetPoints()
{
	TArray<FAirfoilPointData> ResultPoints;
	TArray<FAirfoilPointData*> RowPoints;
	if (Profile == nullptr) {
		return ResultPoints;  // Profile (DataTable точок профілю) не налаштовано
	}

	// Зчитуємо всі рядки таблиці як масив FAirfoilPointData
	Profile->GetAllRows("Get all profile points from Data Table.", RowPoints);
	for (FAirfoilPointData* Point : RowPoints)
	{
		if (Point)
		{
			ResultPoints.Add(*Point);
		}
	}
	return ResultPoints;
}

UControlSurfaceSC* UAerodynamicSurfaceSC::FindControlSurface(EFlapType Type, bool bMirror)
{
	// Шукаємо перший компонент з відповідним типом і прапором дзеркальності
	for (UControlSurfaceSC* Surface : ControlSurfaces)
	{
		if (Surface &&
			Surface->FlapType == Type &&
			Surface->IsMirror == bMirror)
		{
			return Surface;
		}
	}

	return nullptr;
}
