#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Math/RandomStream.h"
#include "UAVSimulator/Structure/CustomSurroundingObject.h"

#include "YoloMarkerDatasetActor.generated.h"

class ACesiumGeoreference;
class ACesium3DTileset;
class ACesiumCameraManager;
class FJsonValue;

/**
 * Інструмент-актор (редактор/рантайм) — аналог ADroneDatasetGeneratorActor для міток
 * на карті (інструмент "Spherical Contour"). Замість того щоб обходити заспавнений
 * Blueprint дрона і виділяти силует, цей актор обходить USceneCaptureComponent2D
 * навколо кожного маркера з JSON-джерела UCustomSurroundingsScannerComponent і
 * формує датасет для детекції у форматі YOLO: один RGB-кадр на кожну позицію камери
 * плюс піксельний bounding box КОЖНОГО маркера, видимого в цьому кадрі.
 *
 * Джерело маркерів: та сама JSON-схема, що й у UCustomSurroundingsScannerComponent —
 * масив {elementId, type, altitude, bbox:{x_min,x_max,y_min,y_max}}, де кожен кут —
 * пара {latitude, longitude} (див. Tools/TestingPlatform/attitude_control/
 * marker/map_objects.json; "altitude" необов'язковий). Кожен кут переводиться у
 * світові координати через ACesiumGeoreference
 * і "прив'язується" прямо вниз/вгору до поверхні тайлу Cesium (ResolveGroundHeights),
 * точно так само, як це робить сканер, — JSON-поле "altitude" для розміщення не
 * використовується.
 *
 * Кутове покриття: кожен маркер по черзі стає центром обходу, тож для нього
 * гарантовано повне сканування — азимут 0..360 з кроком AzimuthStep, висота
 * (elevation) від ElevationMinDeg до ElevationMaxDeg з кроком ElevationStep, один
 * раз для кожного елемента OrbitRadiiMeters. Маркери, що випадково потрапляють у
 * кадр іншого маркера, також отримують мітку — безкоштовно.
 *
 * Модель виконання: НЕ блокуючий цикл. GenerateDataset() вмикає Tick і керує
 * невеликим кінцевим автоматом — перемістити камеру, зачекати SettleFrames тіків
 * (щоб Cesium встиг підвантажити тайли під нову позицію), CaptureScene() +
 * ReadPixels, спроєктувати + зберегти, перейти далі. Оскільки потрібен "живий"
 * потоковий світ, актор має працювати під час Play (із секції меню Synthetic
 * Data), як і інші інструменти, що спираються на сенсори.
 */
UCLASS(Blueprintable)
class UAVSIMULATOR_API AYoloMarkerDatasetActor : public AActor
{
	GENERATED_BODY()

public:
	AYoloMarkerDatasetActor();

	virtual void Tick(float DeltaSeconds) override;

	// ── Джерело маркерів ─────────────────────────────────────────────────────────
	/** Абсолютний шлях до JSON-файлу зі схемою UCustomSurroundingsScannerComponent
	 *  (простий масив або обгортка { "objects": [...] } / { "markers": [...] }).
	 *  Якщо шлях задано і файл читається, він має пріоритет над ObjectsJsonInline.
	 *  За замовчуванням — Tools/TestingPlatform/attitude_control/marker/map_objects.json
	 *  у проєкті; якщо цього шляху немає, LoadObjects() також пробує застарілий
	 *  .../attitude_control/map_objects.json. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Source")
	FString ObjectsSourceFilePath;

	/** Резервний inline-варіант, що використовується, коли ObjectsSourceFilePath
	 *  порожній або нечитабельний. Та сама схема, що й у
	 *  UCustomSurroundingsScannerComponent::ObjectsJson. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Source", meta = (MultiLine = true))
	FString ObjectsJsonInline;

	/** Якщо не порожнє, обходиться лише маркер із цим "elementId" (проте він
	 *  усе одно позначається відносно кожного іншого маркера, що потрапляє в кадр).
	 *  Зручно для швидкого ітеративного тестування. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Source")
	FString OnlyMarkerId;

	/** 0 = усі. Інакше залишаються лише перші N розпарсених маркерів — для швидких
	 *  тестів "на диму". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Source", meta = (ClampMin = 0))
	int32 MaxMarkers = 0;

	// ── Камера ────────────────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 32))
	int32 RenderWidth = 1280;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 32))
	int32 RenderHeight = 720;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Camera", meta = (ClampMin = 10.0, ClampMax = 150.0))
	float CameraFOV = 70.0f;

	// ── Прохід (sweep) ─────────────────────────────────────────────────────────────────
	/** Відстані камера-маркер, у метрах. Для кожного значення виконується повний
	 *  прохід по сфері азимут/висота, тож більше радіусів → більше варіативності
	 *  масштабу в датасеті. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep")
	TArray<float> OrbitRadiiMeters;

	/** Крок азимута в градусах навколо локальної осі "вгору" маркера. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 1.0, ClampMax = 180.0))
	float AzimuthStep = 45.0f;

	/** Найнижче кільце висоти (elevation), у градусах над локальним горизонтом
	 *  (0 = на одному рівні з маркером). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0, ClampMax = 89.0))
	float ElevationMinDeg = 20.0f;

	/** Найвище кільце висоти, у градусах (90 = прямо вниз на маркер). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 1.0, ClampMax = 90.0))
	float ElevationMaxDeg = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 1.0, ClampMax = 90.0))
	float ElevationStep = 25.0f;

	/** Випадкове тремтіння (± градусів) курсу/тангажу, що додається до напрямку
	 *  погляду, щоб ціль не завжди була точно по центру — це покращує тренування
	 *  детектора. 0 вимикає ефект. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0, ClampMax = 30.0))
	float AimJitterDeg = 4.0f;

	/** Витягнути кожен чотирикутник основи на цю висоту вздовж локальної вертикалі
	 *  перед проєкцією, щоб box охоплював висоту будівлі, а не лише її контур на
	 *  землі. 0 = лише основа (як у UCustomSurroundingsScannerComponent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0))
	float MarkerHeightMeters = 0.0f;

	/** Підняти весь купол обходу на цю висоту над маркером уздовж локальної
	 *  вертикалі. Камера все одно наводиться на сам маркер; це лише піднімає
	 *  траєкторію, щоб кільця з малою висотою утворювали піднесену півсферу,
	 *  а не "ковзали" по поверхні тайлу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0))
	float DomeLiftMeters = 5.0f;

	/** Мінімальний вертикальний зазор (у метрах) між камерою і поверхнею тайлу
	 *  Cesium прямо під/над нею. Кожна поза підіймається строго вгору вздовж
	 *  локальної вертикалі, доки не досягне цього зазору, тож камера ніколи не
	 *  опускається під рельєф і не лежить на ньому. 0 вимикає перевірку. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Sweep", meta = (ClampMin = 0.0))
	float CameraGroundClearanceMeters = 15.0f;

	// ── Прийняття міток ──────────────────────────────────────────────────────
	/** Відкинути box маркера, якщо його ширина або висота на екрані менша за це
	 *  значення (у пікселях). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter", meta = (ClampMin = 0.0))
	float MinBBoxPixels = 12.0f;

	/** Маркер отримує мітку, лише якщо принаймні ця частка його спроєктованого box
	 *  лежить у кадрі (0.5 = має бути видно більше половини; 1 = повністю на екрані).
	 *  Маркери, що проходять перевірку, записуються з box, ОБРІЗАНИМ по межах кадру,
	 *  тож частково видимий маркер отримує box, що охоплює лише його видиму частину. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MinVisibleFraction = 0.5f;

	/** Відкинути весь кадр, якщо цільовий маркер обходу сам не отримав валідний
	 *  box (пропускає пози, де рельєф/інші будівлі закривають ціль). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter")
	bool bRequireTargetVisible = true;

	/** Позначати маркер лише за умови, що камера має чисту лінію видимості до
	 *  нього. Без цього кожен маркер, чия основа потрапляє у фрустум, отримує
	 *  box — навіть якщо він схований за рельєфом чи будівлями. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter")
	bool bRequireLineOfSight = true;

	/** Допуск (у метрах) при порівнянні дистанції влучання променя видимості з
	 *  відстанню до маркера — компенсує точки, "прив'язані" до землі й розташовані
	 *  впритул до тайлу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter", meta = (ClampMin = 0.0))
	float LineOfSightToleranceMeters = 3.0f;

	/** Відкинути box маркера, якщо він далі від камери, ніж це значення (у метрах).
	 *  0 вимикає обмеження за відстанню. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Filter", meta = (ClampMin = 0.0))
	float MaxLabelDistanceMeters = 0.0f;

	// ── Прив'язка до землі (дзеркалить UCustomSurroundingsScannerComponent) ──────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Ground")
	bool bSnapMarkersToTileSurface = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Ground")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Ground", meta = (ClampMin = 1.0))
	float GroundTraceSpanMeters = 2000.0f;

	/** За один тік намагатися прив'язати до землі не більше цієї кількості ще
	 *  не розв'язаних маркерів (окрім цілі поточного кадру, для якої спроба
	 *  виконується завжди). Це не дає великому файлу маркерів запускати сотні
	 *  складних трасувань променів щокадру. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Ground", meta = (ClampMin = 1))
	int32 MaxGroundResolvesPerTick = 6;

	/** Відмовитися від прив'язки маркера, що не є ціллю, після такої кількості
	 *  невдалих спроб (коли не всі його кути влучають у тайл). Такий маркер
	 *  залишається без мітки замість того, щоб трасуватися нескінченно. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Ground", meta = (ClampMin = 1))
	int32 MaxGroundResolveAttempts = 16;

	// ── Таймінги ────────────────────────────────────────────────────────────────
	/** Мінімальна кількість тіків утримання кожної пози перед захопленням кадру
	 *  (дає камері перемістися, а реєстрації вигляду Cesium — поширитися).
	 *  Реальне очікування регулюється прогресом завантаження тайлів нижче —
	 *  це лише нижня межа. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Timing", meta = (ClampMin = 1))
	int32 SettleFrames = 4;

	/** Жорстка межа тіків очікування для однієї пози. Якщо набір тайлів так і не
	 *  повідомить про готовність за цю кількість тіків, кадр все одно
	 *  захоплюється (і це логується). У режимі офлайн-тайлів нижче
	 *  GetLoadProgress() зазвичай сходиться, тож ця межа рідко досягається. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Timing", meta = (ClampMin = 1))
	int32 MaxSettleFrames = 90;

	/** Захоплювати кадр, коли ACesium3DTileset::GetLoadProgress() для вигляду
	 *  цього кадру досягає цього відсотка. Значення трохи менше за 100: за
	 *  ForbidHoles останні "відстаючі" тайли рендеряться як батьківські (без
	 *  прогалин), тож очікування ідеальних 100 лише витрачає тіки даремно. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Timing", meta = (ClampMin = 1.0, ClampMax = 100.0))
	float TileLoadProgressTarget = 97.0f;

	/** Вимагати стан "тайли готові" протягом такої кількості послідовних тіків
	 *  перед захопленням — згладжує короткочасне падіння готовності, коли
	 *  Cesium виявляє нові тайли. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Timing", meta = (ClampMin = 1))
	int32 TileReadyHoldFrames = 2;

	// ── Якість стрімінгу тайлів (гарантія відсутності прогалин) ──────────────────────
	/** На весь час проходу кожен ACesium3DTileset примусово переводиться у
	 *  безшовний режим захоплення: ForbidHoles увімкнено, відсікання за туманом
	 *  вимкнено, відсікання за фрустумом вимкнено, а тайли поза фрустумом
	 *  фіксуються на цій (грубій) похибці екранного простору. Ніщо всередині —
	 *  чи одразу за межами — кадру не може відрендеритися як прогалина, тоді як
	 *  "оболонка" поза видимістю лишається дешевою. Усі початкові прапорці
	 *  відновлюються у FinishGeneration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Tiles", meta = (ClampMin = 1.0))
	double CulledTileScreenSpaceError = 128.0;

	/** Фрустум стрімінгу Cesium реєструється ширшим за захоплюваний FOV на цей
	 *  коефіцієнт, щоб крайові тайли вже були деталізовані на момент "спуску
	 *  затвора". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Tiles", meta = (ClampMin = 1.0, ClampMax = 2.0))
	float CesiumFrustumMargin = 1.25f;

	// ── Вивід ────────────────────────────────────────────────────────────────
	/** Коренева тека датасету. Отримує images/{train,val}/, labels/{train,val}/,
	 *  data.yaml, dataset.json. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	FString OutputRootDir;

	/** Частка збережених кадрів, що йде в images/val (0 = усе в train). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output", meta = (ClampMin = 0.0, ClampMax = 0.9))
	float ValSplit = 0.15f;

	/** Також записувати анотовані PNG (з намальованими box і мітками) у <root>/debug/. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	bool bSaveDebugImages = false;

	/** Зберігати кадри у форматі JPEG (як у пайплайні custom_objects_dataset);
	 *  якщо false — у PNG. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset|Output")
	bool bSaveAsJpeg = true;

	/** Почати прохід. Потребує запущеного (Play) світу — див. коментар до класу. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void GenerateDataset();

	/** Зупинити достроково; усе вже захоплене буде записано в data.yaml / dataset.json. */
	UFUNCTION(CallInEditor, Category = "Dataset")
	void CancelGeneration();

private:
	/** Одна поза камери для рендеру: який маркер є центром обходу і де саме
	 *  розташована камера. */
	struct FShot
	{
		int32 MarkerIdx;
		float RadiusMeters;
		float AzimuthDeg;
		float ElevationDeg;
	};

	/** Один прийнятий box маркера в кадрі — верхній лівий кут + розмір у
	 *  піксельному просторі, а також просторові координати центру маркера, щоб
	 *  детекцію можна було прив'язати назад до конкретного місця. */
	struct FLabel
	{
		int32   ClassId;
		FString Id;
		FString Type;
		float   X;
		float   Y;
		float   W;
		float   H;
		float   VisibleFraction;

		FVector CentreWorldM;   // центр маркера у світових координатах Unreal, метри
		FVector CentreGeo;      // (довгота°, широта°, висота над еліпсоїдом у м)
		FVector CentreCameraM;  // центр маркера в локальних осях камери (X вперед, Y вправо, Z вгору), метри
		double  RangeM;         // відстань по прямій камера → центр маркера, метри
	};

	UPROPERTY() USceneCaptureComponent2D* CaptureComp   = nullptr;
	UPROPERTY() UTextureRenderTarget2D*   RenderTarget  = nullptr;
	UPROPERTY() ACesiumGeoreference*      Georeference  = nullptr;
	UPROPERTY() ACesium3DTileset*         Tileset       = nullptr;

	/** Менеджер камер Cesium для цього світу; прохід передає йому позу захоплення,
	 *  щоб Cesium активно стрімив/деталізував тайли для кожного кадру, а не
	 *  покладався лише на SettleFrames. Розв'язується лениво, самообнуляється. */
	TWeakObjectPtr<ACesiumCameraManager> CesiumCameraManager;

	/** Стабільний id від ACesiumCameraManager::AddCamera; INDEX_NONE, поки не зареєстровано. */
	int32 CesiumCameraId = INDEX_NONE;

	// ── Стан виконання ─────────────────────────────────────────────────────────────
	bool  bRunning           = false;
	int32 ShotCursor         = 0;
	int32 SettleCounter      = 0;
	int32 ReadyStreak        = 0;   // кількість послідовних тіків, коли набір тайлів повідомляв про готовність
	int32 SavedFrameCount    = 0;
	int32 AttemptedShotCount = 0;
	int32 ValEveryN          = 6;
	FRandomStream Rng;

	TArray<FCustomSurroundingObject> Objects;
	TArray<FShot>                    Shots;
	/** Паралельно до Objects: кількість невдалих спроб ResolveGroundHeights на маркер (для дроселювання). */
	TArray<int32>                    GroundResolveAttempts;
	int32                            GroundResolveCursor = 0;  // стартова точка для циклічної (round-robin) прив'язки за тік
	TMap<FString, int32>             ClassMap;    // тип об'єкта → id класу
	TArray<FString>                  ClassNames;  // id класу → назва
	TArray<TSharedPtr<FJsonValue>>   ManifestFrames;

	/** Кожен ACesium3DTileset на рівні плюс прапорці відсікання, які в нього
	 *  були до того, як прохід примусово увімкнув ForbidHoles / вимкнув
	 *  відсікання за туманом. Відновлюються у FinishGeneration. */
	struct FTilesetCulling
	{
		bool   ForbidHoles;
		bool   FogCulling;
		bool   FrustumCulling;
		bool   EnforceCulledSSE;
		double CulledSSE;
	};
	UPROPERTY() TArray<TObjectPtr<ACesium3DTileset>> SweepTilesets;
	TArray<FTilesetCulling>                          SavedTilesetCulling;

	FString ImagesTrainDir;
	FString ImagesValDir;
	FString LabelsTrainDir;
	FString LabelsValDir;
	FString DebugDir;

	/** "<frame_stem> <lat> <lon> <alt_m>" на кожен збережений кадр → <root>/exp_geo_position.txt. */
	TArray<FString> GeoPositionLines;

	// ── Кроки пайплайну ────────────────────────────────────────────────────────
	bool LoadObjects();
	void BuildClassMap();
	void BuildShots();

	FVector GeoToWorldMeters(double LatitudeDeg, double LongitudeDeg, double HeightMeters) const;
	/** Обернена до GeoToWorldMeters: світові метри Unreal → (довгота°, широта°, висота м). */
	FVector WorldMetersToGeographic(const FVector& WorldMeters) const;
	FVector GeographicUpMeters(double LatitudeDeg, double LongitudeDeg) const;
	void    ResolveGroundHeights(FCustomSurroundingObject& Object) const;

	/** Примусово вмикає ForbidHoles / вимикає відсікання за туманом для кожного
	 *  набору тайлів на час проходу (щоб у кадрах ніколи не було чорних прогалин
	 *  між тайлами), а потім відновлює стан. */
	void  BeginTilesetCaptureMode();
	void  EndTilesetCaptureMode();
	/** Найменший GetLoadProgress() серед усіх наборів тайлів проходу — кадр
	 *  захоплюється лише тоді, коли це значення досягає TileLoadProgressTarget.
	 *  100, якщо наборів тайлів немає. */
	float MinTilesetLoadProgress() const;

	void PlaceCameraForShot(const FCustomSurroundingObject& Target, const FShot& Shot);
	/** Підіймає позицію камери строго вгору вздовж UpDir, доки вона не відступить
	 *  від поверхні тайлу Cesium на CameraGroundClearanceMeters. Повертає
	 *  (можливо, підняту) позицію. */
	FVector LiftAboveTileSurface(const FVector& CamPosCm, const FVector& UpDir) const;
	void SyncCesiumCaptureCamera();        // додає або оновлює FCesiumCamera за поточною позою
	void UnregisterCesiumCaptureCamera();  // прибирає її, коли прохід завершується
	bool ProjectMetersToPixels(const FVector& WorldMeters, FVector2D& OutPixels) const;
	bool ComputeMarkerLabel(const FCustomSurroundingObject& Object, FLabel& OutLabel) const;
	/** True, якщо камера має неперешкоджену лінію видимості до будь-якої тестової точки маркера. */
	bool IsMarkerVisibleFromCamera(const FCustomSurroundingObject& Object) const;

	void ProcessCurrentShot();
	bool SaveFrame(const TArray<FColor>& Pixels, const TArray<FLabel>& Labels,
	               const FShot& Shot, const FString& TargetId);
	void WriteDatasetYaml() const;
	void WriteManifest() const;
	/** classes.json + virtual_map.json (id → lat/lon кожного маркера) + exp_geo_position.txt. */
	void WriteReferenceFiles() const;
	void FinishGeneration(bool bCancelled);
};
