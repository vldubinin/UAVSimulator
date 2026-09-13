#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Structure/CustomSurroundingObject.h"
#include "CustomSurroundingsScannerComponent.generated.h"

class USceneCaptureComponent2D;
class ACesiumGeoreference;
class ACesium3DTileset;

/**
 * Та сама роль і форма, що й у UCesiumSurroundingsScannerComponent, але навколишні об'єкти
 * беруться з фіксованого списку JSON (ObjectsJson — наразі захардкоджено, заглушка для
 * майбутнього джерела на основі файлу/мережі) замість розгортки Cesium 3D Tiles. Кожен запис
 * JSON виглядає так:
 *
 *   {
 *     "elementId": "obj3",
 *     "type": "building",
 *     "bbox": {
 *       "x_min": { "latitude": 50.4094702, "longitude": 30.6122947 },
 *       "x_max": { "latitude": 50.4094685, "longitude": 30.6127346 },
 *       "y_min": { "latitude": 50.4097497, "longitude": 30.6127507 },
 *       "y_max": { "latitude": 50.4097548, "longitude": 30.6123148 }
 *     },
 *     "altitude": 350
 *   }
 *
 * ObjectsJson розпарсовується в BeginPlay (LoadObjects()): кожен кут "bbox" (x_min, x_max,
 * y_min, y_max) перетворюється у світову позицію через
 * ACesiumGeoreference::TransformLongitudeLatitudeHeightPositionToUnreal — див. AllObjects.
 * Центр відбитка (середнє чотирьох кутів) стає Latitude/Longitude/WorldLocationMeters.
 * TickComponent перепарсовує ObjectsJson щоразу, коли рядок змінюється (див.
 * LastLoadedObjectsJson), тож редагування кутів запису, типу чи висоти діє одразу, без
 * перезапуску.
 *
 * "altitude" із JSON навмисно НЕ використовується для розміщення маркерів. Натомість кожен
 * Scan() виконує вертикальну трасу лінією (ResolveGroundHeights/TryTraceTileSurfaceMeters) по
 * тайлах Cesium нижче/вище кожного кута і прив'язує кут — а через середнє й центр відбитка —
 * до поверхні тайла, тож маркери завжди лежать точно на висоті рельєфу. Траса повторюється
 * щотіку, доки не влучить, оскільки Cesium стрімить тайли за дистанцією до камери.
 *
 * Кожен Scan() заново перевіряє кожен завантажений об'єкт відносно поточного вигляду камери:
 * у межах дальності (ScanRadiusMeters) і проєкція в кадр камери (ProjectWorldToScreen —
 * ідентична математика вигляду/проєкції до UCesiumSurroundingsScannerComponent). Тут немає ні
 * фізичної розгортки, ні перевірки оклюзії — це відомі, точні точки, а не влучання, отримані
 * розгорткою по мешу, тож пряма перевірка щотіку сама по собі є авторитетною.
 *
 * Об'єкти, що наразі проходять перевірку, звіряються з ObjectStorage — постійною мапою наразі
 * видимих об'єктів, так само як UCesiumSurroundingsScannerComponent звіряє власний
 * ObjectStorage:
 *   1. Об'єкт, який щойно почав проходити перевірку, додається через AddObject() (і логується).
 *   2. Об'єкт, що перестав проходити перевірку, видаляється через RemoveObject().
 *   3. ObjectStorage змінюється лише через AddObject()/RemoveObject().
 *   4. LatestScanResults, відладочні промені та JSON-навантаження сенсора — все керується з
 *      ObjectStorage, а не напряму з AllObjects.
 *
 * Georeference використовується лише для перетворення широти/довготи/висоти кожного об'єкта у
 * світову позицію (LoadObjects()) — жодної подальшої взаємодії з геометрією тайлів Cesium
 * немає.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCustomSurroundingsScannerComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UCustomSurroundingsScannerComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("custom_objects"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

public:
	/**
	 * Заново перевіряє кожен об'єкт з AllObjects відносно поточної дальності/кадру камери,
	 * звіряє ObjectStorage із результатом (додаючи щойно видимі об'єкти, відкидаючи ті, що
	 * покинули кадр або дальність), оновлює LatestScanResults з ObjectStorage і повертає
	 * посилання на нього. Викликається автоматично з TickComponent; також може бути викликаний
	 * напряму з Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Custom Surroundings")
	const TArray<FCustomSurroundingObject>& Scan();

	/** Дзеркало ObjectStorage — кожен завантажений об'єкт, що наразі у полі зору. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Custom Surroundings")
	TArray<FCustomSurroundingObject> LatestScanResults;

	/**
	 * Вихідний JSON — масив об'єктів {elementId, type, bbox, altitude}, де "bbox" — це
	 * { x_min, x_max, y_min, y_max }, кожен — пара { latitude, longitude } (див. коментар класу
	 * та Tools/TestingPlatform/attitude_control/map_objects.json). Наразі захардкоджено як
	 * редагований дефолт; заглушка для майбутнього джерела на основі файлу/мережі.
	 * Розпарсовується в BeginPlay через LoadObjects() і перепарсовується в TickComponent щоразу,
	 * коли цей рядок змінюється.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings", meta = (MultiLine = true))
	FString ObjectsJson;

	// ── Параметри сканування ───────────────────────────────────────────────────────

	/** Максимальна дальність виявлення, у метрах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings", meta = (ClampMin = 1.0f))
	float ScanRadiusMeters = 10000.0f;

	// ── Прив'язка до землі ───────────────────────────────────────────────────────
	// Маркери розміщуються на поверхні тайла Cesium, а не на "altitude" з JSON (яка ігнорується
	// для розміщення). Висота знаходиться вертикальною трасою по тайлах, що повторюється кожен
	// Scan(), доки не влучить — тайли стрімляться за дистанцією до камери.

	/** Головний перемикач прив'язки до поверхні тайла. Вимкнено ⇒ маркери лишаються на сирій геореференсній висоті. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Ground")
	bool bSnapMarkersToTileSurface = true;

	/** Канал колізії, на якому виконується траса до землі — має бути таким, який блокує тайлсет Cesium. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Ground")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	/** Половина довжини вертикальної траси до землі, у метрах, по обидва боки від геореференсної точки. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Ground", meta = (ClampMin = 1.0f))
	float GroundTraceSpanMeters = 20000.0f;

	/** Наскільки маркери піднімаються над поверхнею тайла вздовж локальної осі "вгору", у метрах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Ground")
	float GroundHeightOffsetMeters = 0.0f;

	/**
	 * Обмежує прийняте влучання в землю лише акторами класу ACesium3DTileset. Залиште увімкненим
	 * для звичайного використання; вимкніть для діагностики (наприклад, колізія тайлсету
	 * налаштована на іншому акторі, або потрібно прив'язуватися до будь-якої блокуючої геометрії
	 * на GroundTraceChannel).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Ground")
	bool bRequireCesiumTilesetHit = true;

	/**
	 * Малює кожну трасу до землі: сегмент траси (зелений, якщо влучив, червоний — якщо
	 * промахнувся) плюс сферу в точці влучання. Також логує прогрес розв'язання кожного об'єкта
	 * в LogUAV. Увімкнено при початковому налаштуванні прив'язки; вимкніть, коли висоти
	 * розв'язуються коректно.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Ground")
	bool bDebugGroundTrace = true;

	// ── Відладочні маркери ──────────────────────────────────────────────────────────
	// Малюються лише поки bSensorEnabled — true, тож вимикаються разом із компонентом (так само,
	// як JSON-навантаження сенсора в BuildSensorFrame), а не рендеряться завжди.

	/** Колір відладочного променя, що малюється від поточної позиції літака до кожного видимого об'єкта. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	FColor RayDebugColor = FColor::Yellow;

	/**
	 * Малює каркас поточного фрустуму огляду камери щоскану: чотири ребра від камери до дальніх
	 * кутів на ScanRadiusMeters, плюс дальній прямокутник, що їх з'єднує. Та сама форма/угода, що
	 * й у bDrawScanArea компонента UCesiumSurroundingsScannerComponent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	bool bDrawScanArea = true;

	/** Колір каркаса зони сканування (див. bDrawScanArea). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	FColor ScanAreaDebugColor = FColor::Cyan;

	/**
	 * Малює чотирикутник відбитка об'єкта — чотири кути "bbox" (BBoxCornersWorldMeters), ребро
	 * до ребра в порядку обходу — див. DrawObjectBBoxDebug().
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	bool bDrawObjectBBox = true;

	/** Колір чотирикутника bbox об'єкта (див. bDrawObjectBBox). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	FColor BBoxDebugColor = FColor::Green;

	/** Малює "elementId" (ObjectID) кожного видимого об'єкта як текст над його точкою. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	bool bDrawObjectLabel = true;

	/** Колір тексту мітки elementId (див. bDrawObjectLabel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug")
	FColor LabelDebugColor = FColor::White;

	/** Масштаб шрифту тексту мітки elementId (див. bDrawObjectLabel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Surroundings|Debug", meta = (ClampMin = 0.1f))
	float LabelFontScale = 1.5f;

private:
	/**
	 * Розпарсовує ObjectsJson у AllObjects, перетворюючи чотири кути "bbox" кожного запису у
	 * світові координати через Georeference (BBoxCornersWorldMeters) і беручи їхнє середнє як
	 * центр відбитка (Latitude/Longitude/WorldLocationMeters). Викликається з BeginPlay, а також
	 * повторно з TickComponent щоразу, коли ObjectsJson більше не збігається з
	 * LastLoadedObjectsJson — тож редагування під час гри діє з наступного тіку.
	 */
	void LoadObjects();

	/**
	 * Географічні координати (широта, довгота в градусах; висота над еліпсоїдом у метрах) у
	 * світову позицію в метрах, через Georeference. Винесено з LoadObjects(), щоб траса
	 * прив'язки до землі могла це перевикористовувати. Повертає ZeroVector, якщо Georeference ще
	 * не розв'язаний.
	 */
	FVector GeoToWorldMeters(double LatitudeDeg, double LongitudeDeg, double HeightMeters) const;

	/**
	 * Світовий напрямок "вгору" (одиничний вектор) у заданій географічній точці — нормаль
	 * еліпсоїда, знайдена перетворенням точки на двох висотах і нормалізацією різниці.
	 * Повертає FVector::UpVector, якщо Georeference не розв'язаний. Використовується як вісь
	 * траси в TryTraceTileSurfaceMeters, щоб вертикаль була географічно коректною, а не просто
	 * світовою +Z.
	 */
	FVector GeographicUpMeters(double LatitudeDeg, double LongitudeDeg) const;

	/**
	 * Вертикальна траса лінією (уздовж UpMeters, ±GroundTraceSpanMeters) від FromPointMeters по
	 * тайлах Cesium на GroundTraceChannel. При першому блокуючому влученні, що потрапило на
	 * Tileset (або будь-якому блокуючому влученні, якщо Tileset не знайдено), записує точку
	 * влучання — піднятою на GroundHeightOffsetMeters уздовж осі "вгору" — в
	 * OutSurfacePointMeters і повертає true. "altitude" з JSON ніколи не враховується: висота
	 * поверхні тайла є авторитетною.
	 */
	bool TryTraceTileSurfaceMeters(const FVector& FromPointMeters, const FVector& UpMeters, FVector& OutSurfacePointMeters) const;

	/**
	 * Прив'язує кожен маркер Object до поверхні тайла Cesium: кожен із чотирьох
	 * BBoxCornersWorldMeters через TryTraceTileSurfaceMeters, потім WorldLocationMeters як їхнє
	 * середнє. Встановлює Object.bGroundHeightResolved, щойно кожен кут отримав влучання; доки
	 * цього не сталося, повторюється на наступному Scan() (тайли віддаленого об'єкта можуть ще
	 * не бути застрімлені). Викликається з Scan(), поки bSnapMarkersToTileSurface — true.
	 */
	void ResolveGroundHeights(FCustomSurroundingObject& Object) const;

	/**
	 * Малює каркас bDrawScanArea (див. коментар до нього): чотири ребра від Origin до дальніх
	 * кутів на Range, плюс дальній прямокутник, що їх з'єднує. Ідентична логіка форми до
	 * UCesiumSurroundingsScannerComponent::DrawScanAreaDebug, без обмеження нижньої половини (тут
	 * немає сітки-розгортки променів, яку потрібно було б обрізати).
	 */
	void DrawScanAreaDebug(const FTransform& OriginTransform, float Range, float HalfHFovRad, float HalfVFovRad) const;

	/**
	 * Малює CornersMeters (чотири кути "bbox" об'єкта, у метрах) як замкнутий чотирикутник —
	 * кожен кут масштабується до см і з'єднується з наступним по порядку. Без білбордингу: це
	 * реальні світові точки, тож чотирикутник лежить на фактичному відбитку об'єкта.
	 */
	void DrawObjectBBoxDebug(const TArray<FVector>& CornersMeters) const;

	/**
	 * Проєктує кожну з CornersMeters (у метрах) на кадр камери через
	 * ProjectWorldToScreenUnclamped, потім повертає ширину/висоту в піксельному просторі їхніх
	 * осьовирівняних меж — та сама проєкція, що використовується для pixel_x/pixel_y, без
	 * обмеження межами сенсора, тож частково поза кадром bbox все одно повідомляє свій справжній
	 * розмір. Кут позаду камери пропускається; якщо всі позаду — повертає (0, 0).
	 */
	FVector2D ComputeBBoxScreenSize(const TArray<FVector>& CornersMeters) const;

	/** Малює Label як текст у WorldPositionCm (див. bDrawObjectLabel). */
	void DrawObjectLabelDebug(const FVector& WorldPositionCm, const FString& Label) const;

	/** Операція 1/2 над ObjectStorage: реєструє щойно видимий об'єкт і логує його виявлення. */
	void AddObject(const FString& Key, const FCustomSurroundingObject& Entry);

	/** Операція 2/2 над ObjectStorage: забуває об'єкт, щойно він покинув кадр камери або дальність. */
	void RemoveObject(const FString& Key);

	/**
	 * Будує та кешує JSON-навантаження IUAVSensorInterface для цього тіку з ObjectStorage — по
	 * одному об'єкту на кожен наразі видимий запис, з id/type/latitude/longitude/altitude плюс
	 * pixel_x/pixel_y/visible з ProjectWorldToScreen, bboxw/bboxh як піксельний розмір
	 * проєктованого чотирикутника відбитка (ComputeBBoxScreenSize над
	 * BBoxCornersWorldMeters), і corners_px як чотири кути відбитка, проєктовані окремо
	 * (плоский масив [x0,y0,x1,y1,...], порядок обходу x_min -> x_max -> y_min -> y_max, без
	 * обмеження, кут позаду камери — як [-1,-1]), щоб споживач міг підігнати орієнтований бокс,
	 * а не лише осьовирівняний.
	 * Викликається наприкінці TickComponent, після Scan(); нічого не робить (і скидає
	 * закешований кадр), доки bSensorEnabled — false.
	 */
	void BuildSensorFrame();

	/**
	 * Лінькаво зчитує SensorSizeX/SensorSizeY з TextureTarget компонента SceneCaptureComponent.
	 * Винесено окремо від BuildSensorFrame() (яка виконується лише поки bSensorEnabled — true),
	 * щоб ProjectWorldToScreen мав дійсні розміри сенсора для перевірки видимості в Scan()
	 * незалежно від того, чи увімкнена публікація сенсора через ZMQ. Викликається безумовно на
	 * початку TickComponent, перед Scan().
	 */
	void UpdateSensorSize();

	/**
	 * Проєктує одну точку у світових координатах (в см Unreal) на рендер-таргет
	 * SceneCaptureComponent. Ідентичне налаштування матриці вигляду/проєкції до
	 * UCesiumSurroundingsScannerComponent::ProjectWorldToScreen. Повертає false (не видимо),
	 * якщо розмір рендер-таргета захоплення ще не відомий, точка позаду камери, або вона
	 * виходить за межі рендер-таргета. Тонка обгортка над ProjectWorldToScreenUnclamped, що
	 * додає перевірку меж.
	 */
	bool ProjectWorldToScreen(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const;

	/**
	 * Ядро ProjectWorldToScreen, без перевірки меж сенсора: те саме налаштування матриці
	 * вигляду/проєкції, але OutScreenPos повертається як є, навіть коли виходить за межі
	 * рендер-таргета (використовується ComputeBBoxScreenSize, де кут поза кадром все ще
	 * значущий). Повертає false лише тоді, коли розмір захоплення/сенсора ще не відомий або
	 * точка позаду камери.
	 */
	bool ProjectWorldToScreenUnclamped(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const;

	/** Захоплення сцени власника — надає трансформ, з якого проєктується перевірка видимості. */
	UPROPERTY()
	USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	/**
	 * Розв'язується в BeginPlay через ACesiumGeoreference::GetDefaultGeoreference.
	 * Використовується LoadObjects() для перетворення широти/довготи кожного запису у світову
	 * позицію, а трасою прив'язки до землі — для визначення локальної осі "вгору".
	 */
	UPROPERTY()
	ACesiumGeoreference* Georeference = nullptr;

	/**
	 * Перший ACesium3DTileset у світі, розв'язується в BeginPlay. Траса прив'язки до землі
	 * приймає лише влучання, що потрапило саме на цього актора; якщо жодного не знайдено,
	 * замість цього приймається будь-яке блокуюче влучання на GroundTraceChannel.
	 */
	UPROPERTY()
	ACesium3DTileset* Tileset = nullptr;

	/**
	 * Кожен об'єкт, розпарсований з ObjectsJson, із попередньо обчисленим WorldLocationMeters.
	 * Перебудовується LoadObjects() (BeginPlay, і повторно щоразу, коли змінюється
	 * ObjectsJson); DistanceMeters додатково оновлюється кожен Scan().
	 */
	TArray<FCustomSurroundingObject> AllObjects;

	/** ObjectsJson станом на останній виклик LoadObjects() — дозволяє TickComponent виявляти зміни на льоту. */
	FString LastLoadedObjectsJson;

	/** Ігровий час останнього обмеженого за частотою логу "траса до землі не влучила" (див. TryTraceTileSurfaceMeters). */
	mutable double LastGroundTraceLogTime = -100.0;

	/**
	 * Постійне сховище наразі видимих об'єктів, ключоване за ObjectID. Змінюється лише через
	 * AddObject()/RemoveObject() — див. документацію класу. LatestScanResults, консольний лог,
	 * відладочні промені та навантаження сенсора — все керується з цього сховища, а не напряму
	 * з AllObjects.
	 */
	TMap<FString, FCustomSurroundingObject> ObjectStorage;

	/** Роздільна здатність рендер-таргета SceneCaptureComponent — лінькаво зчитується в UpdateSensorSize(). */
	int32 SensorSizeX = 0;
	int32 SensorSizeY = 0;

	// Останній серіалізований кадр IUAVSensorInterface — записується й читається лише в ігровому потоці.
	TArray<uint8> LatestPayload;
	double        LatestTimestamp = 0.0;
	bool          bHasFrame       = false;
};
