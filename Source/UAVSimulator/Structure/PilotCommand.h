#pragma once

#include "CoreMinimal.h"
#include "PilotCommand.generated.h"

/**
 * Нормалізований намір керування від одного джерела вводу (IPilotInputSource) за кадр.
 * Аналог FSensorFrame для сенсорної шини — джерела заповнюють цю структуру, координатор
 * (UPilotInputComponent) зводить кілька таких і пише результат у UFlightDynamicsComponent.
 */
USTRUCT(BlueprintType)
struct UAVSIMULATOR_API FPilotCommand
{
	GENERATED_BODY()

	/** Намір елеронів [-1, 1] (0 — нейтраль). */
	UPROPERTY(BlueprintReadOnly)
	float Roll = 0.f;

	/** Намір керма висоти [-1, 1] (0 — нейтраль). «Сирий»: інверсію робить координатор. */
	UPROPERTY(BlueprintReadOnly)
	float Pitch = 0.f;

	/** Намір керма напрямку [-1, 1] (0 — нейтраль). */
	UPROPERTY(BlueprintReadOnly)
	float Yaw = 0.f;

	/** Інкрементний намір газу [-1, 1]: додається до акумулятора координатора помножений на dt. */
	UPROPERTY(BlueprintReadOnly)
	float ThrottleRate = 0.f;

	/** >= 0 => джерело задає положення газу абсолютно [0, 1] (напр. автопілот). < 0 => не задає. */
	UPROPERTY(BlueprintReadOnly)
	float ThrottleAbsolute = -1.f;

	/** true, якщо джерело справді щось дає цього кадру (інакше координатор його ігнорує). */
	UPROPERTY(BlueprintReadOnly)
	bool bHasInput = false;
};
