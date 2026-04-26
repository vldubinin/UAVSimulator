#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UAVSimulator/Entity/Chord.h"
#include "UAVSimulator/DataAsset/AerodynamicProfileRow.h"
#include "AerodynamicPhysicsLibrary.generated.h"

/**
 * Stateless aerodynamics math utilities.
 * Methods with Blueprint-compatible signatures are exposed as BlueprintCallable.
 * Methods taking FAerodynamicProfileRow* are plain static C++ only.
 */
UCLASS()
class UAVSIMULATOR_API UAerodynamicPhysicsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Обчислює кут атаки в градусах між вектором набігаючого повітря та хордою профілю.
	 * Використовує atan2 проекцій потоку на вектор вгору та вздовж хорди.
	 * @param WorldAirVelocity      — вектор швидкості повітря у світових координатах (cm/s).
	 * @param AverageChordDirection — нормалізований напрямок середньої хорди поверхні.
	 * @param SurfaceUpVector       — вектор «вгору» поверхні у світових координатах.
	 * @return Кут атаки в градусах (від −180 до +180).
	 */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float CalculateAngleOfAttack(FVector WorldAirVelocity, FVector AverageChordDirection, FVector SurfaceUpVector);

	/**
	 * Обчислює підйомну силу в Ньютонах за формулою L = CL * q * S.
	 * Повертає 0 і записує попередження якщо Profile == nullptr.
	 * @param AoA            — кут атаки в градусах.
	 * @param DynamicPressure — динамічний тиск q = 0.5 * ρ * V² (Па).
	 * @param SurfaceArea    — площа поверхні в cm².
	 * @param Profile        — рядок полярних даних; може бути nullptr.
	 * @return Підйомна сила в Ньютонах.
	 */
	static float CalculateLift(float AoA, float DynamicPressure, float SurfaceArea, FAerodynamicProfileRow* Profile);

	/**
	 * Обчислює силу аеродинамічного опору в Ньютонах за формулою D = CD * q * S.
	 * Повертає 0 і записує попередження якщо Profile == nullptr.
	 * @param AoA            — кут атаки в градусах.
	 * @param DynamicPressure — динамічний тиск q (Па).
	 * @param SurfaceArea    — площа поверхні в cm².
	 * @param Profile        — рядок полярних даних; може бути nullptr.
	 * @return Сила опору в Ньютонах.
	 */
	static float CalculateDrag(float AoA, float DynamicPressure, float SurfaceArea, FAerodynamicProfileRow* Profile);

	/**
	 * Обчислює тангажний момент в Н·м за формулою M = CM * q * S * c.
	 * Повертає 0 і записує попередження якщо Profile == nullptr.
	 * @param AoA            — кут атаки в градусах.
	 * @param DynamicPressure — динамічний тиск q (Па).
	 * @param SurfaceArea    — площа поверхні в cm².
	 * @param ChordLength    — середня довжина хорди в м.
	 * @param Profile        — рядок полярних даних; може бути nullptr.
	 * @return Тангажний момент в Н·м.
	 */
	static float CalculateTorque(float AoA, float DynamicPressure, float SurfaceArea, float ChordLength, FAerodynamicProfileRow* Profile);

	/**
	 * Конвертує довжину вектора швидкості з cm/s у м/с.
	 * @param WorldAirVelocity — вектор швидкості у cm/s.
	 * @return Швидкість у м/с.
	 */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float VelocityToMetersPerSecond(FVector WorldAirVelocity);

	/**
	 * Перетворює силу з Ньютонів у внутрішні одиниці Unreal (kg·cm/s²).
	 * Коефіцієнт: 1 N = 100 kg·cm/s².
	 * @param Newtons — сила в Ньютонах.
	 * @return Сила у kg·cm/s².
	 */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float NewtonsToKiloCentimeter(float Newtons);

	/**
	 * Обчислює середню довжину хорди між двома хордами в метрах.
	 * Точки хорд задаються в cm.
	 * @param FirstChord  — перша хорда (початок і кінець у cm).
	 * @param SecondChord — друга хорда (початок і кінець у cm).
	 * @return Середня довжина хорди в м.
	 */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float CalculateAverageChordLength(FChord FirstChord, FChord SecondChord);

	/**
	 * Визначає напрямок підйомної сили як векторний добуток вектора розмаху та напрямку опору.
	 * @param WorldAirVelocity — вектор швидкості повітря (визначає напрямок опору).
	 * @param SpanDirection    — вектор розмаху поверхні у світових координатах.
	 * @return Нормалізований вектор напрямку підйомної сили.
	 */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static FVector CalculateLiftDirection(FVector WorldAirVelocity, FVector SpanDirection);

	/**
	 * Знаходить центр тиску як лінійну інтерполяцію між серединами хорд на заданому відсотку.
	 * @param StartChord       — хорда початку розмаху.
	 * @param EndChord         — хорда кінця розмаху.
	 * @param PercentageOffset — зміщення від передньої кромки у відсотках (0–100).
	 * @return Центр тиску у локальних координатах (cm).
	 */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static FVector FindCenterOfPressure(FChord StartChord, FChord EndChord, float PercentageOffset);

	/**
	 * Обчислює площу чотирикутника, утвореного двома хордами, через суму двох трикутників.
	 * @param StartChord — хорда початку розмаху.
	 * @param EndChord   — хорда кінця розмаху.
	 * @return Площа в cm².
	 */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static float CalculateQuadSurfaceArea(FChord StartChord, FChord EndChord);

	/**
	 * Лінійно інтерполює точку на відрізку між StartPoint та EndPoint.
	 * @param StartPoint — початок відрізка.
	 * @param EndPoint   — кінець відрізка.
	 * @param Alpha      — параметр інтерполяції [0, 1] (затискається).
	 * @return Точка на відрізку.
	 */
	UFUNCTION(BlueprintCallable, Category = "UAV|Aerodynamics")
	static FVector GetPointOnLineAtPercentage(FVector StartPoint, FVector EndPoint, float Alpha);
};
