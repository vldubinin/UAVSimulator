#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/Chord.h"

class USceneComponent;

/**
 * Static helpers for visualising aerodynamic surfaces and forces in the editor and PIE.
 * All functions are no-ops in shipping builds.
 */
class UAVSIMULATOR_API AerodynamicDebugRenderer
{
public:
	/**
	 * Відображає аеродинамічну поверхню: сплайни профілів початку та кінця розмаху,
	 * а також лінії передньої та задньої кромок між ними.
	 * @param Parent       — батьківський компонент для прикріплення сплайнів.
	 * @param StartChord   — хорда початку розмаху.
	 * @param EndChord     — хорда кінця розмаху.
	 * @param StartProfile — точки профілю на початку розмаху (локальний простір).
	 * @param EndProfile   — точки профілю на кінці розмаху (локальний простір).
	 * @param Name         — базове ім'я для іменування утворених компонентів.
	 */
	static void DrawSurface(USceneComponent* Parent,
		FChord StartChord, FChord EndChord,
		const TArray<FVector>& StartProfile, const TArray<FVector>& EndProfile,
		FName Name);

	/**
	 * Створює USplineComponent, прикріплений до Parent, і додає задані точки як вузли сплайну.
	 * @param Parent  — батьківський компонент.
	 * @param Points  — послідовність точок у локальному просторі.
	 * @param Name    — ім'я нового SplineComponent.
	 */
	static void DrawSpline(USceneComponent* Parent, const TArray<FVector>& Points, FName Name);

	/**
	 * Відображає лінію шарніра закрилка між StartChord і EndChord на заданих відсоткових позиціях.
	 * Нічого не робить якщо MinFlapAngle == 0 і MaxFlapAngle == 0 (закрилок відсутній).
	 * @param Parent            — батьківський компонент.
	 * @param StartChord        — хорда початку розмаху.
	 * @param EndChord          — хорда кінця розмаху.
	 * @param StartFlapPosition — позиція шарніра на початку хорди (відсоток від 0 до 100).
	 * @param EndFlapPosition   — позиція шарніра на кінці хорди (відсоток від 0 до 100).
	 * @param MinFlapAngle      — мінімальний кут закрилка.
	 * @param MaxFlapAngle      — максимальний кут закрилка.
	 * @param Name              — базове ім'я компонента.
	 */
	static void DrawFlap(USceneComponent* Parent,
		FChord StartChord, FChord EndChord,
		float StartFlapPosition, float EndFlapPosition,
		float MinFlapAngle, float MaxFlapAngle,
		FName Name);

	/**
	 * Відображає хрест з трьох осей у заданій точці локального простору.
	 * @param Parent — батьківський компонент.
	 * @param Point  — центр хреста у локальному просторі.
	 * @param Name   — базове ім'я компонента.
	 */
	static void DrawCrosshairs(USceneComponent* Parent, FVector Point, FName Name);

	/**
	 * Прикріплює UTextRenderComponent із заданим текстом поблизу точки у локальному просторі.
	 * @param Parent   — батьківський компонент.
	 * @param Text     — рядок тексту для відображення.
	 * @param Point    — базова точка у локальному просторі.
	 * @param Offset   — зміщення від Point.
	 * @param Rotator  — орієнтація тексту.
	 * @param Color    — колір тексту.
	 * @param Name     — ім'я нового TextRenderComponent.
	 */
	static void DrawLabel(USceneComponent* Parent,
		FString Text, FVector Point, FVector Offset,
		FRotator Rotator, FColor Color, FName Name);

	/**
	 * Малює направлену стрілку у світовому просторі для візуалізації вектора сили.
	 * @param World     — поточний світ.
	 * @param Origin    — точка прикладання сили.
	 * @param Direction — вектор сили (масштабується на Scale).
	 * @param Color     — колір стрілки.
	 * @param Scale     — множник довжини стрілки.
	 * @param ArrowSize — розмір наконечника стрілки.
	 * @param Thickness — товщина лінії.
	 * @param Duration  — тривалість відображення (-1 = постійно).
	 */
	static void DrawForceArrow(const UWorld* World,
		FVector Origin, FVector Direction,
		FColor Color, float Scale,
		float ArrowSize, float Thickness, float Duration);

private:
	static FVector GetPointOnLineAtPercentage(FVector Start, FVector End, float Alpha);
};
