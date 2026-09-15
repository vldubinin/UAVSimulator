#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UAVSimulator/Structure/EWZoneConfiguration.h"
#include "EnvironmentActorManager.generated.h"

class AEWZoneActor;
class ACesiumGeoreference;

/**
 * Сцена-актор, що керує наземними об'єктами середовища, налаштованими через зовнішній
 * інструмент Tools/ProjectTools/configurate_env_actors.py. Кнопка ConfigurateEnvActorsBtn
 * у UEnvironmentSectionWidget викликає OpenConfigurationTool() — той спершу записує поточний
 * EWConfigurations у Tools/ProjectTools/env_actors.json (щоб скрипт одразу відмалював наявні
 * зони на карті), тоді запускає скрипт (карта Tkinter) синхронно через
 * AerodynamicToolRunner::RunPythonScript і, щойно вікно карти закриють, перечитує той самий
 * файл (куди SAVE у скрипті перезаписує масиви об'єктів) та синхронізує EWConfigurations +
 * спавнені AEWZoneActor.
 *
 * env_actors.json наразі містить лише масив "electronic_warfare"; коли зі скрипта додадуть
 * інші типи об'єктів, для кожного заводиться свій масив конфігурацій і своя Refresh*-логіка
 * за тим самим принципом.
 */
UCLASS()
class UAVSIMULATOR_API AEnvironmentActorManager : public AActor
{
	GENERATED_BODY()

public:
	AEnvironmentActorManager();

	/** Блупрінт-клас (на основі AEWZoneActor), що спавниться для кожного запису в EWConfigurations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW")
	TSubclassOf<AEWZoneActor> EWZoneActorClass;

	/** Набір конфігурацій зон РЕБ на сцені — джерело правди для спавнених AEWZoneActor.
	 *  Оновлюється з env_actors.json після SAVE у configurate_env_actors.py, або редагується
	 *  вручну в редакторі — обидва шляхи одразу перебудовують сцену через RefreshEWZones()
	 *  і записуються назад у env_actors.json, тому конфігурація зберігається між запусками
	 *  гри незалежно від того, чи відкривали інструмент карти в поточній сесії. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW")
	TArray<FEWZoneConfiguration> EWConfigurations;

	/** Записує поточний EWConfigurations у env_actors.json, запускає configurate_env_actors.py
	 *  (стартові координати карти — з ACesiumGeoreference цього рівня; наявні зони скрипт
	 *  одразу підхопить і відмалює з щойно записаного файлу) і, щойно користувач закриє вікно
	 *  карти, підвантажує (можливо оновлені через SAVE у скрипті) конфігурації назад. */
	UFUNCTION(BlueprintCallable, Category = "EW")
	void OpenConfigurationTool();

	/** Перечитує Tools/ProjectTools/env_actors.json, оновлює EWConfigurations і викликає
	 *  RefreshEWZones(). Нічого не робить (з попередженням у лог) і повертає false, якщо
	 *  файла ще нема або він не парситься — це нормально, доки жодного разу не зберігали
	 *  конфігурацію (ні через SAVE у скрипті, ні через редактор).
	 *  @return true якщо файл знайдено, розпарсено й EWConfigurations застосовано. */
	UFUNCTION(BlueprintCallable, Category = "EW")
	bool LoadConfigurationsFromFile();

	/** Записує поточний EWConfigurations у Tools/ProjectTools/env_actors.json — тим самим
	 *  форматом, що й SAVE у configurate_env_actors.py, щоб скрипт міг підхопити його при
	 *  наступному відкритті. */
	UFUNCTION(BlueprintCallable, Category = "EW")
	void SaveConfigurationsToFile() const;

	/** Знищує всі раніше спавнені цим менеджером AEWZoneActor і спавнить нові — по одному
	 *  на кожен запис у EWConfigurations, з відповідною позицією (SetGeoPosition) і радіусом
	 *  (SetRadius, метри з конфігурації переводяться в см). */
	UFUNCTION(BlueprintCallable, Category = "EW")
	void RefreshEWZones();

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	ACesiumGeoreference* GetGeoreference() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AEWZoneActor>> SpawnedEWZones;
};
