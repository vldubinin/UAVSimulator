#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "EnvironmentSectionWidget.generated.h"

class USpinBox;
class UCheckBox;
class UButton;
class ACesiumGeoreference;
class ACesiumSunSky;
class ACesium3DTileset;
class AEnvironmentActorManager;
class ARainEffectManager;

UCLASS()
class UAVSIMULATOR_API UEnvironmentSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void OnSectionActivated_Implementation() override;

	// — CesiumGeoreference (початок координат) ——————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginLatitude;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginLongitude;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginHeight;

	// — CesiumSunSky (сонце/небо) ————————————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxTimeZone;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxSolarTime;

	// — Зоряне небо (StarsSphere / MI_Stars, керується з UpdateNightVisuals) ———————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxStarsDensity;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxStarsThreshold;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxStarsPointSize;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxStarsIntensity;

	// — Cesium3DTileset (тайли ландшафту) ————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> TerrainSurfaceCB;

	// — Налаштування наземних об'єктів (map-marker) ————————————————————————————
	// Позиція/радіус зон РЕБ налаштовуються через ConfigurateEnvActorsBtn (карта) і живуть
	// в AEnvironmentActorManager::EWConfigurations.
	// OptionalWidget = true: те саме застереження, що й для EW-віджетів вище — кнопки
	// ще нема в UMG Blueprint, доки її не додадуть вручну.

	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<UButton> ConfigurateEnvActorsBtn;

	// — Дощ (ARainEffectManager) ————————————————————————————————————————————————
	// OptionalWidget = true: спінбоксу ще нема в UMG Blueprint, доки його не додадуть вручну
	// (те саме застереження, що й для ConfigurateEnvActorsBtn вище). 0 = дощу нема взагалі —
	// окремого чекбокса вкл/викл немає, RainIntensity — єдине джерело правди.

	UPROPERTY(meta = (BindWidget, OptionalWidget = true))
	TObjectPtr<USpinBox> SpinBoxRainIntensity;

	// — Резервні небо/сонце, що показуються, поки ландшафт Cesium вимкнено ————————

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Environment|DefaultSky")
	TSubclassOf<AActor> DefaultSkyboxClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Environment|DefaultSky")
	TSubclassOf<AActor> DefaultSunClass;

private:
	void SyncFromWorld();
	void ApplyTerrainSurfaceState(bool bEnabled);
	void LoadAndApplySavedSettings();
	void SaveCurrentSettings();

	// CesiumSunSky обертає лише свій вбудований DirectionalLight (сонце). Другий
	// DirectionalLightComponent (AtmosphereSunLightIndex == 1, "місяць" для нічного
	// неба) рушій не рухає сам — тож синхронізуємо його вручну дзеркально до сонця,
	// і водночас підкручуємо яскравість зоряного неба (StarsSphere, параметр
	// NightFactor матеріалу MI_Stars) відповідно до реального нахилу сонця. Викликається
	// щоразу, коли міняється SolarTime/TimeZone — інакше і місяць, і зорі лишаються
	// в тому стані, в якому їх залишив попередній виклик UpdateSun. Параметри зірок
	// передаються явно (а не читаються зі спінбоксів всередині), щоб цю саму функцію
	// можна було викликати і з LoadAndApplySavedSettings (значеннями з сейву, до того
	// як віджети синхронізовані) і з живих UI-хендлерів.
	void UpdateNightVisuals(ACesiumSunSky* SunSky, float StarsDensity, float StarsThreshold, float StarsPointSize, float StarsIntensity) const;

	// Читає поточні значення 4 зоряних спінбоксів і застосовує через UpdateNightVisuals.
	void ApplyStarsSettingsFromWidgets() const;

	ACesiumGeoreference* GetGeoreference() const;
	ACesiumSunSky*       GetSunSky() const;
	ACesium3DTileset*    GetTileset() const;
	AEnvironmentActorManager* GetEnvironmentActorManager() const;
	ARainEffectManager*  GetRainEffectManager() const;

	static const FString EnvironmentSaveSlotName;

	UFUNCTION() void OnOriginLatitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginLongitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginHeightCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTimeZoneCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnSolarTimeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnStarsDensityCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnStarsThresholdCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnStarsPointSizeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnStarsIntensityCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTerrainSurfaceChanged(bool bIsChecked);
	UFUNCTION() void OnRainIntensityCommitted(float Value, ETextCommit::Type CommitType);

	UFUNCTION() void OnConfigurateEnvActorsBtnClicked();

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedDefaultSkybox;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedDefaultSun;
};
