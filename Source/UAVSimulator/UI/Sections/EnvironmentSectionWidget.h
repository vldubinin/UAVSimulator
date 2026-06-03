#pragma once

#include "CoreMinimal.h"
#include "SimulatorSectionWidget.h"
#include "EnvironmentSectionWidget.generated.h"

class USpinBox;
class ACesiumGeoreference;
class ACesiumSunSky;

UCLASS()
class UAVSIMULATOR_API UEnvironmentSectionWidget : public USimulatorSectionWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void OnSectionActivated_Implementation() override;

	// — CesiumGeoreference ————————————————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginLatitude;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginLongitude;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxOriginHeight;

	// — CesiumSunSky ——————————————————————————————————————————————————————————

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxTimeZone;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USpinBox> SpinBoxSolarTime;

private:
	void SyncFromWorld();

	ACesiumGeoreference* GetGeoreference() const;
	ACesiumSunSky*       GetSunSky() const;

	UFUNCTION() void OnOriginLatitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginLongitudeCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnOriginHeightCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnTimeZoneCommitted(float Value, ETextCommit::Type CommitType);
	UFUNCTION() void OnSolarTimeCommitted(float Value, ETextCommit::Type CommitType);
};
