#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UAVSimulator/Structure/PilotCommand.h"
#include "PilotInputSource.generated.h"

class UInputComponent;

UINTERFACE(MinimalAPI, NotBlueprintable)
class UPilotInputSource : public UInterface { GENERATED_BODY() };

/**
 * Контракт джерела керування пілота. Реалізується компонентами на AAirplane
 * (клавіатура, джойстик, автопілот). Координатор UPilotInputComponent авто-дискаверить
 * усі такі компоненти на власнику (як SensorBusComponent робить з IUAVSensorInterface),
 * щокадру опитує їх і зводить у єдиний запис до UFlightDynamicsComponent.
 */
class UAVSIMULATOR_API IPilotInputSource
{
	GENERATED_BODY()
public:
	/**
	 * Керується координатором / pawn. Default false — джерело інертне, доки його явно не ввімкнуть
	 * (клавіатуру/джойстик вмикає координатор у BeginPlay; автопілот — сам у ActivateAutopilot()).
	 */
	bool bInputSourceEnabled = false;

	/** Стабільний ідентифікатор: "keyboard", "gamepad", "autopilot" (лог/дебаг/дискавер). */
	virtual FName GetInputSourceId() const = 0;

	/**
	 * Тир пріоритету. Серед активних джерел координатор бере лише тир із найвищим числом і
	 * підсумовує команди в його межах (clamp [-1,1]). Тож автопілот (tier 100) при активності
	 * повністю перебиває людські джерела (tier 0).
	 */
	virtual int32 GetInputSourcePriority() const = 0;

	/** Прив'язати власні осі/клавіші до InputComponent опанованого pawn. Автопілот — порожньо. */
	virtual void BindInput(UInputComponent* InputComponent) = 0;

	/**
	 * Заповнити OutCommand наміром цього кадру. Повертає OutCommand.bHasInput.
	 * Викликається в ігровому потоці координатором щотіку.
	 */
	virtual bool GetPilotCommand(FPilotCommand& OutCommand) = 0;
};
