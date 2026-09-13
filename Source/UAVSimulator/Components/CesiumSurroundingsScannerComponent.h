#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "UAVSimulator/Interfaces/UAVSensorInterface.h"
#include "UAVSimulator/Structure/CesiumSurroundingObject.h"
#include "CesiumSurroundingsScannerComponent.generated.h"

class UUAVCameraComponent;
class USceneCaptureComponent2D;
class ACesium3DTileset;
class UPrimitiveComponent;

/**
 * Проводить сканування невеликою сферою (SweepMultiByChannel) уздовж сітки напрямків, що
 * охоплює точно горизонтальне/вертикальне поле зору бортової камери актора-власника
 * (UUAVCameraComponent/USceneCaptureComponent2D), із власного трансформа камери — тому
 * сканування знаходить кожен об'єкт Cesium, який бачить камера, і нічого поза її полем зору.
 * Кожен напрям променя проводить розгортку товстою сферою (SweepRadius), а не трасуванням
 * лінією нульової товщини, тому проміжки між сусідніми за кутом променями не дають об'єктам
 * "проскочити" на великій дальності. Для кожного влучання, що потрапляє на об'єкт Cesium 3D
 * Tiles із таблицею властивостей — налаштованою на компоненті CesiumFeaturesMetadataComponent
 * тайлсету за
 * https://cesium.com/learn/unreal/unreal-visualize-metadata — зчитує метадані цього об'єкта
 * (наприклад, Longitude/Latitude/Height) через
 * UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit. Влучання, що
 * потрапляють на той самий об'єкт, об'єднуються в один запис за скан.
 *
 * Об'єднані записи потім звіряються з ObjectStorage — постійною мапою наразі відстежуваних
 * об'єктів:
 *   1. Об'єкт, що вже є у сховищі, залишається незмінним, доки в нього продовжують влучати —
 *      його дані заморожені такими, якими вони були побачені вперше, навіть якщо влучання
 *      наступного скану трохи відрізняється.
 *   2. Об'єкт, у який розгортка цього скану не влучила (наприклад, миттєвий проміжок між
 *      променями або об'єкт, затулений чимось іншим), НЕ видаляється одразу: його заморожена
 *      світова позиція перепроєктується на камеру, і він залишається у сховищі — все ще
 *      "відтворюваним" — доки ця проєкція потрапляє в межі екрана камери. Лише коли його
 *      заморожена позиція справді виходить за межі кадру (або опиняється позаду камери), його
 *      видаляють.
 *   3. ObjectStorage змінюється лише через AddObject() (щойно побачений об'єкт) та
 *      RemoveObject() (об'єкт, чия заморожена позиція покинула кадр).
 *   4. Все, що нижче за течією — консольний лог і відладочні промені (по одному променю на
 *      кожен відстежуваний об'єкт, що перемальовуються щотіку від поточної позиції літака,
 *      та сама техніка picking, що й у
 *      https://cesium.com/learn/unreal/unreal-visualize-metadata/) — керується з ObjectStorage,
 *      а не зі свіжого результату розгортки поточного скану.
 */
UCLASS(ClassGroup = (UAV), meta = (BlueprintSpawnableComponent))
class UAVSIMULATOR_API UCesiumSurroundingsScannerComponent : public UActorComponent, public IUAVSensorInterface
{
	GENERATED_BODY()

public:
	UCesiumSurroundingsScannerComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── IUAVSensorInterface ───────────────────────────────────────────────────
	virtual FString GetSensorTopic() const override { return TEXT("cesium_objects"); }
	virtual bool GetLatestFrame(FSensorFrame& OutFrame) override;

protected:
	virtual void BeginPlay() override;

public:
	/**
	 * Виконує сканування негайно в ігровому потоці, звіряє ObjectStorage із тим, що зараз видно
	 * (додає щойно побачені об'єкти, видаляє ті, що вже поза полем зору), оновлює
	 * LatestScanResults з ObjectStorage і повертає посилання на нього. Викликається автоматично
	 * з TickComponent з частотою ScanRate Hz; також може бути викликаний напряму з Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cesium Surroundings")
	const TArray<FCesiumSurroundingObject>& Scan();

	/** Дзеркало ObjectStorage — кожен наразі видимий об'єкт із його замороженими даними першого спостереження. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cesium Surroundings")
	TArray<FCesiumSurroundingObject> LatestScanResults;

	// ── Параметри сканування ───────────────────────────────────────────────────────

	/** Максимальна дальність виявлення, у метрах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1.0f))
	float ScanRadiusMeters = 10000.0f;

	/** Кількість променів, рівномірно розподілених по горизонтальному FOV камери. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1))
	int32 HorizontalRays = 72;

	/** Кількість променів, рівномірно розподілених по вертикальному FOV камери. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1))
	int32 VerticalLayers = 8;

	/** Скільки повних сканувань виконується за секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 0.01f, ClampMax = 100.0f))
	float ScanRate = 1.0f;

	/**
	 * Після цієї кількості викликів Scan() нижня половина вертикального FOV (VAngleRad < 0,
	 * тобто все нижче поздовжньої осі камери) назавжди виключається зі сітки розгортки — далі
	 * скануватиметься лише верхня половина. 0 вимикає це обмеження, тож повний вертикальний FOV
	 * скануватиметься завжди.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 0))
	int32 FramesBeforeLowerHalfCutoff = 10;

	/** Канал колізії, що використовується для розгорток. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;

	/**
	 * Радіус (см) сфери, що розгортається уздовж кожного напрямку променя. Надає кожному
	 * променю товщину, щоб об'єкти між двома сусідніми за кутом променями не пропускалися на
	 * великій дальності — збільшуйте це значення, якщо повне сканування у стилі лідара все ще
	 * лишає проміжки, зменшуйте — щоб знизити кількість дублікатів/накладених влучань.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 1.0f))
	float SweepRadius = 100.0f;

	/**
	 * Який набір ідентифікаторів об'єктів зчитувати на влученій примітиві (індекс у
	 * CesiumPrimitiveFeatures примітива). Відповідає "Feature ID Set Index", який
	 * використовується у "Get Property Table Values From Hit" в Blueprint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings", meta = (ClampMin = 0))
	int64 FeatureIDSetIndex = 0;

	// ── Відладочні промені ─────────────────────────────────────────────────────────────

	/** Колір відладочного променя, що малюється від поточної позиції літака до кожного сканованого об'єкта. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug")
	FColor RayDebugColor = FColor::Yellow;

	/**
	 * Малює каркас поточної зони сканування щоскану: чотири ребра від початку координат до
	 * дальніх кутів на ScanRadiusMeters, плюс дальній прямокутник, що їх з'єднує. Відображає
	 * FramesBeforeLowerHalfCutoff у реальному часі: щойно обмеження спрацьовує, нижнє ребро
	 * лягає на поздовжню вісь камери замість нижньої межі FOV, тому каркас візуально
	 * стискається до верхньої половини.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug")
	bool bDrawScanArea = true;

	/** Колір каркаса зони сканування (див. bDrawScanArea). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Debug")
	FColor ScanAreaDebugColor = FColor::Cyan;

	// ── Вихідні дані сенсора (IUAVSensorInterface) ────────────────────────────────────
	// Назви ключів таблиці властивостей, що зчитуються як шир./довг./висота. Налаштовувані,
	// бо різні тайлсети називають ці властивості по-різному (наприклад, "lat"/"lon" проти
	// "Latitude"/"Longitude").

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Sensor")
	FString ObjectPropertyName = TEXT("elementId");

	/** Ключ таблиці властивостей, що зчитується як широта об'єкта (у градусах). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Sensor")
	FString LatitudePropertyName = TEXT("cesium#latitude");

	/** Ключ таблиці властивостей, що зчитується як довгота об'єкта (у градусах). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Sensor")
	FString LongitudePropertyName = TEXT("cesium#longitude");

	/** Ключ таблиці властивостей, що зчитується як висота об'єкта (у метрах). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium Surroundings|Sensor")
	FString AltitudePropertyName = TEXT("Height");

private:
	/**
	 * Розгортає сферу (SweepRadius) уздовж сітки напрямків HorizontalRays x VerticalLayers, що
	 * охоплює точно HorizontalFOVDeg x VerticalFOVDeg компонента CameraComponent, на дальність
	 * ScanRadiusMeters, від OriginTransform (власний трансформ камери — див. Scan()). Кожен
	 * напрямок побудований так, щоб його горизонтальний/вертикальний кут відносно поздовжньої
	 * осі OriginTransform точно потрапляв у [-HalfFOV, +HalfFOV] — та сама перевірка
	 * прямокутного фрустуму, яку використовує перспективна камера — тож ніщо поза полем зору
	 * камери ніколи не сканується. Перед розгорткою широкофазний прохід
	 * (GatherNearbyTileComponents/BuildActiveCellSet) звужує сітку лише до тих клітинок, що
	 * потенційно можуть влучити у вже завантажений тайл Cesium, тож клітинки, спрямовані у
	 * порожнє небо/землю, взагалі не витрачають ресурси на фізичний запит. Повертає кожне
	 * влучання з кожної розгортки (один напрямок може повернути кілька перекриваних влучань,
	 * так само як зазвичай робить SweepMultiByChannel).
	 */
	TArray<FHitResult> SweepScan(const FTransform& OriginTransform, AActor* ActorToIgnore) const;

	/**
	 * Малює каркас bDrawScanArea (див. коментар до нього): чотири ребра від Origin до дальніх
	 * кутів на Range, плюс дальній прямокутник, що їх з'єднує. VMinRad/VMaxRad — це фактичні
	 * вертикальні межі для цього скану: VMinRad дорівнює 0 замість -HalfVFovRad, щойно
	 * FramesBeforeLowerHalfCutoff спрацював, тож каркас звужується точно відповідно до того,
	 * що зараз охоплює сітка SweepScan.
	 */
	void DrawScanAreaDebug(const FTransform& OriginTransform, float Range, float HalfHFovRad, float VMinRad, float VMaxRad) const;

	/**
	 * Збір кандидатів широкофазного проходу, без фізики: кожна наразі завантажена примітива
	 * тайла під Tileset, чия обмежувальна сфера в межах RangeCm від Origin. Cesium уже сам
	 * відсікає тайли за фрустумом при завантаженні/стрімінгу, тож це просто дешева перевірка
	 * меж серед уже наявних компонентів тайлів, дочірніх до актора тайлсету.
	 */
	TArray<UPrimitiveComponent*> GatherNearbyTileComponents(const FVector& Origin, float RangeCm) const;

	/**
	 * Проєктує обмежувальну сферу кожного кандидата в простір горизонтального/вертикального
	 * кута OriginTransform (та сама угода про кути, яку використовує сітка SweepScan) і
	 * позначає кожну клітинку сітки, чий напрямок потрапляє в кутовий відбиток кандидата.
	 * SweepScan розгортає лише клітинки з повернутого набору. Кандидат, що охоплює або лежить
	 * позаду площини камери, обережно активує всю сітку, щоб не ризикувати мовчазно його
	 * втратити.
	 */
	TSet<int32> BuildActiveCellSet(const FTransform& OriginTransform, const TArray<UPrimitiveComponent*>& Candidates,
		float HalfHFovRad, float HalfVFovRad) const;

	/**
	 * Кілька напрямків розгортки часто потрапляють на той самий фізичний об'єкт (наприклад,
	 * кілька точок на одному фасаді будівлі), оскільки таблиця властивостей розрізняє лише
	 * об'єкти, а не окремі трикутники. Об'єднує записи, що мають той самий актор/компонент і
	 * однакові метадані, в один запис, усереднюючи їхні позиції влучання й зберігаючи
	 * найменшу дистанцію, тож кожна будівля дає один запис у сховищі замість кількох.
	 */
	static TArray<FCesiumSurroundingObject> MergeDuplicateHits(const TArray<FCesiumSurroundingObject>& RawEntries);

	/** Стабільна ідентичність об'єкта — той самий актор/компонент + однакові метадані — використовується як ключ ObjectStorage. */
	static FString BuildFeatureKey(const FCesiumSurroundingObject& Entry);

	/** Операція 1/2 над ObjectStorage: реєструє щойно побачений об'єкт і логує його виявлення. */
	void AddObject(const FString& Key, const FCesiumSurroundingObject& Entry);

	/** Операція 2/2 над ObjectStorage: забуває об'єкт, щойно його заморожена позиція покинула кадр. */
	void RemoveObject(const FString& Key);

	/**
	 * Будує та кешує JSON-корисне навантаження IUAVSensorInterface для цього тіку з
	 * ObjectStorage — по одному об'єкту на кожен наразі видимий об'єкт, кожен зі стабільним id,
	 * широтою/довготою/висотою, розібраними з Metadata (через LatitudePropertyName/
	 * LongitudePropertyName/AltitudePropertyName), та pixel_x/pixel_y/visible з
	 * ProjectWorldToScreen. Викликається наприкінці TickComponent, після Scan(); нічого не
	 * робить (і скидає закешований кадр), доки bSensorEnabled — false.
	 */
	void BuildSensorFrame();

	/**
	 * Лінькаво зчитує SensorSizeX/SensorSizeY з TextureTarget компонента SceneCaptureComponent.
	 * Винесено окремо від BuildSensorFrame() (який виконується лише поки bSensorEnabled — true),
	 * щоб ProjectWorldToScreen мав дійсні розміри сенсора для перевірки Scan() "чи все ще в
	 * кадрі?" незалежно від того, чи увімкнена публікація сенсора через ZMQ. Викликається
	 * безумовно на початку TickComponent, перед Scan().
	 */
	void UpdateSensorSize();

	/**
	 * Проєктує одну точку у світових координатах (в см Unreal) на рендер-таргет
	 * SceneCaptureComponent. Ідентичне налаштування матриці вигляду/проєкції до
	 * UKeyPointDetectionComponent::ProjectWorldToScreen — див. цю реалізацію щодо перевірок
	 * "позаду камери" (W<=0) та потрапляння в межі. Повертає false (точка не видима), якщо
	 * розмір рендер-таргета захоплення ще не відомий.
	 *
	 * Також слугує перевіркою Scan() "чи цей збережений об'єкт усе ще в кадрі?": заморожений
	 * запис ObjectStorage, у який розгортка цього скану не влучила, видаляється лише тоді,
	 * коли його HitLocationMeters проєктується за межі повернутих меж (або позаду камери).
	 */
	bool ProjectWorldToScreen(const FVector& WorldPositionCm, FVector2D& OutScreenPos) const;

	/** Бортова камера власника — надає HorizontalFOVDeg/VerticalFOVDeg для сітки сканування. */
	UPROPERTY()
	UUAVCameraComponent* CameraComponent = nullptr;

	/** Захоплення сцени власника — надає трансформ (позицію + орієнтацію), з якого проводиться сканування. */
	UPROPERTY()
	USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	/**
	 * Перший знайдений у світі ACesium3DTileset — надає завантажені компоненти тайлів для
	 * широкофазного відсіювання в SweepScan. Null — безпечний резервний варіант: SweepScan
	 * просто сканує повну сітку, як і раніше без цієї оптимізації.
	 */
	UPROPERTY()
	ACesium3DTileset* Tileset = nullptr;

	/**
	 * Постійне сховище наразі відстежуваних об'єктів, ключоване через BuildFeatureKey(). Об'єкт
	 * залишається тут — з замороженими даними — навіть протягом сканувань, які в нього не
	 * влучають, поки його заморожена позиція все ще проєктується в кадр камери (див.
	 * ProjectWorldToScreen). Змінюється лише через AddObject()/RemoveObject() — див.
	 * документацію класу. LatestScanResults, консольний лог і відладочні промені — усі
	 * керовані з цього сховища, а не із сирого результату розгортки поточного скану.
	 */
	TMap<FString, FCesiumSurroundingObject> ObjectStorage;

	/** Кількість завершених викликів Scan() дотепер; порівнюється з FramesBeforeLowerHalfCutoff. */
	int32 ScanCount = 0;

	/** Роздільна здатність рендер-таргета SceneCaptureComponent — лінькаво зчитується в BuildSensorFrame(), оскільки UAVCameraComponent призначає TextureTarget у власному BeginPlay. */
	int32 SensorSizeX = 0;
	int32 SensorSizeY = 0;

	// Останній серіалізований кадр IUAVSensorInterface — записується й читається лише в ігровому потоці.
	TArray<uint8> LatestPayload;
	double        LatestTimestamp = 0.0;
	bool          bHasFrame       = false;
};
