#include "StreetLightsManager.h"
#include "UAVSimulator/UAVSimulator.h"
#include "UAVSimulator/Actor/Airplane.h"
#include "UAVSimulator/Components/CustomSurroundingsScannerComponent.h"
#include "UAVSimulator/Components/CesiumSurroundingsScannerComponent.h"

#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

namespace
{
	// User Parameter (Float, [0,1]), який має бути експонований у StreetLightsSystem і
	// використовуватись (типово — множником на розмір/яскравість спрайта) — інакше
	// SetBrightness() ні на що не вплине, той самий принцип, що IntensityParameterName в
	// ARainEffectManager.
	const FName BrightnessParameterName(TEXT("Brightness"));

	// Niagara Array Data Interface (Vector3), у який штовхається плаский масив позицій вогнів
	// (світові см) — той самий підхід, що WakePositions у AeroVisualizerComponent.
	const FName LightPositionsParameterName(TEXT("LightPositions"));

	// User Parameter (Int32) — точна кількість вогнів (TrackedBuildingsMap-сумарно), прив'язана
	// до SpawnBurst_Instantaneous::Spawn Count. Разовий burst (не continuous SpawnRate) — щоб
	// весь масив з'являвся одразу, а не поступово заповнювався; Lifetime частинки тепер
	// величезний (86400с), тож між повними Activate(true) вони не гинуть і не "перетасовуються"
	// самі по собі — див. коментар StreetLightsSystem у .h.
	const FName TargetSpawnCountParameterName(TEXT("TargetSpawnCount"));
}

AStreetLightsManager::AStreetLightsManager()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* DefaultRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRoot"));
	RootComponent = DefaultRoot;
}

void AStreetLightsManager::BeginPlay()
{
	Super::BeginPlay();

	if (StreetLightsSystem)
	{
		// bAutoDestroy=false — часом життя керує сам менеджер (EndPlay), як RainSystem у
		// ARainEffectManager. bAutoActivate=false — активація/деактивація йде виключно через
		// SetBrightness().
		NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), StreetLightsSystem, GetActorLocation(), FRotator::ZeroRotator, FVector(1.0f),
			/*bAutoDestroy*/ false, /*bAutoActivate*/ false, ENCPoolMethod::None, /*bPreCullCheck*/ true);
	}
	else
	{
		UE_LOG(LogUAV, Warning, TEXT("StreetLightsManager: StreetLightsSystem не задано — нічого не спавню"));
	}

	// Актор міг бути вручну розміщений у рівні з ненульовою Brightness (EditAnywhere) — застосовуємо
	// її тепер, коли NiagaraComp уже існує (SetBrightness(), викликаний до BeginPlay ззовні, застав
	// би NiagaraComp ще null).
	if (Brightness > 0.0f)
	{
		SetBrightness(Brightness);
	}
}

void AStreetLightsManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Дешево: лише читання вже обчисленого UCustomSurroundingsScannerComponent::LatestScanResults
	// (фактичне сканування/ground-snap виконує сам сканер).
	Scan();

	if (bLightPositionsDirty)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastRebuildTimeSeconds >= MinRebuildIntervalSeconds)
		{
			RebuildNiagaraArrays();
			bLightPositionsDirty = false;
			LastRebuildTimeSeconds = Now;
		}
	}

	if (bDrawDebugFootprints)
	{
		UWorld* World = GetWorld();
		for (const TPair<FString, FStreetLightBuilding>& Pair : TrackedBuildingsMap)
		{
			const FStreetLightBuilding& Building = Pair.Value;
			const int32 CornerCount = Building.FootprintCornersWorldMeters.Num();
			for (int32 CornerIndex = 0; CornerIndex < CornerCount; ++CornerIndex)
			{
				const FVector From = Building.FootprintCornersWorldMeters[CornerIndex] * 100.0;
				const FVector To   = Building.FootprintCornersWorldMeters[(CornerIndex + 1) % CornerCount] * 100.0;
				DrawDebugLine(World, From, To, FootprintDebugColor, false, -1.0f);
			}
			for (const FVector& LightPositionMeters : Building.LightPositionsWorldMeters)
				DrawDebugSphere(World, LightPositionMeters * 100.0, 100.0f, 8, LightDebugColor, false, -1.0f);
		}
	}
}

void AStreetLightsManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (NiagaraComp)
	{
		NiagaraComp->DestroyComponent();
	}

	Super::EndPlay(EndPlayReason);
}

void AStreetLightsManager::SetBrightness(float NewBrightness)
{
	Brightness = FMath::Clamp(NewBrightness, 0.0f, 100.0f);

	UE_LOG(LogUAV, Log, TEXT("StreetLightsManager::SetBrightness: %.1f"), Brightness);

	if (!NiagaraComp)
		return;

	if (Brightness <= 0.0f)
	{
		NiagaraComp->Deactivate();
		return;
	}

	NiagaraComp->SetFloatParameter(BrightnessParameterName, Brightness / 100.0f);
	NiagaraComp->Activate();
}

void AStreetLightsManager::SetDataSource(EStreetLightsDataSource NewDataSource)
{
	if (DataSource == NewDataSource)
		return;

	DataSource = NewDataSource;
	UE_LOG(LogUAV, Log, TEXT("StreetLightsManager::SetDataSource: %s"),
		*StaticEnum<EStreetLightsDataSource>()->GetNameStringByValue((int64)DataSource));

	// Різні джерела — різні ObjectID і геометрія: старі вогні скидаємо, нові набереться з
	// наступних Scan(). Пушимо масив негайно (без тротлінгу), щоб старі вогні зникли одразу.
	TrackedBuildingsMap.Reset();
	RebuildNiagaraArrays();
	bLightPositionsDirty = false;
	LastRebuildTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : LastRebuildTimeSeconds;
}

// ─────────────────────────────────────────────────────────────────────────────
// Сканування — споживає вже перевірений робочий UCustomSurroundingsScannerComponent (лениво
// доданий на кожен AAirplane) замість власного sweep/метадані. Див. коментар класу в .h.
// ─────────────────────────────────────────────────────────────────────────────

void AStreetLightsManager::Scan()
{
	UWorld* World = GetWorld();
	if (!World)
		return;

	TArray<AActor*> Airplanes;
	UGameplayStatics::GetAllActorsOfClass(World, AAirplane::StaticClass(), Airplanes);

	TArray<FVector> AirplaneLocationsCm;
	TMap<FString, TArray<FVector>> FreshFootprints; // ObjectID -> BBoxCornersWorldMeters (метри)

	for (AActor* Airplane : Airplanes)
	{
		if (!Airplane)
			continue;

		AirplaneLocationsCm.Add(Airplane->GetActorLocation());

		if (DataSource == EStreetLightsDataSource::Cesium)
		{
			// Cesium-сканер вимагає бортової камери (сітка променів = FOV камери), тому сам
			// не створюємо його — беремо лише той, що вже є на літаку (Blueprint).
			const UCesiumSurroundingsScannerComponent* CesiumScanner = Airplane->FindComponentByClass<UCesiumSurroundingsScannerComponent>();
			if (!CesiumScanner)
				continue;

			for (const FCesiumSurroundingObject& Object : CesiumScanner->LatestScanResults)
			{
				// Метадані не дають контуру — лише точка влучання (усереднена по влученнях на
				// будівлю). Навколо неї будуємо прямокутник випадкового (але стабільного для
				// цього ObjectID) розміру й повороту й вважаємо його контуром будівлі.
				FreshFootprints.Add(Object.ObjectID, MakeRandomFootprintAround(Object.ObjectID, Object.HitLocationMeters));
			}
			continue;
		}

		UCustomSurroundingsScannerComponent* Scanner = GetOrCreateScannerFor(Airplane);
		if (!Scanner)
			continue;

		for (const FCustomSurroundingObject& Object : Scanner->LatestScanResults)
		{
			if (!Object.bGroundHeightResolved || Object.BBoxCornersWorldMeters.Num() < 3)
				continue; // footprint ще не прив'язано до рельєфу — почекаємо наступного скану

			FreshFootprints.Add(Object.ObjectID, Object.BBoxCornersWorldMeters);
		}
	}

	int32 NewlyAdded = 0;
	for (const TPair<FString, TArray<FVector>>& Pair : FreshFootprints)
	{
		if (TrackedBuildingsMap.Contains(Pair.Key))
			continue; // вже відстежується — дані заморожені, той самий принцип, що ObjectStorage

		FStreetLightBuilding Building;
		if (BuildLightsForObject(Pair.Key, Pair.Value, Building))
		{
			AddBuilding(Pair.Key, Building);
			++NewlyAdded;
		}
	}

	RunValiditySweep(AirplaneLocationsCm);

	if (NewlyAdded > 0)
	{
		UE_LOG(LogUAV, Log, TEXT("StreetLightsManager::Scan: %d літак(и), %d об'єктів від сканера, %d нових, усього відстежується %d"),
			Airplanes.Num(), FreshFootprints.Num(), NewlyAdded, TrackedBuildingsMap.Num());
	}
}

UCustomSurroundingsScannerComponent* AStreetLightsManager::GetOrCreateScannerFor(AActor* Airplane)
{
	if (!Airplane)
		return nullptr;

	if (TWeakObjectPtr<UCustomSurroundingsScannerComponent>* Found = OwnScannersByAirplane.Find(Airplane))
	{
		if (Found->IsValid())
			return Found->Get();
		OwnScannersByAirplane.Remove(Airplane); // застарілий запис (літак чи компонент знищено) — знайдемо/створимо знову нижче
	}

	// Використовуємо вже наявний сканер на літаку (типово — заздалегідь розміщений на
	// Blueprint, для реальної сенсорної шини), якщо він є: тоді вогні автоматично йдуть із ТИХ
	// САМИХ даних/налаштувань (ObjectsJson, радіус тощо), які вже видно на екрані через
	// debug-промені цього сканера — жодного дублювання конфігурації. Свій ScanRadiusMeters/
	// GroundTraceChannel і власні debug-прапорці застосовуємо лише тоді, коли на літаку
	// взагалі ще нема жодного (створюємо з нуля).
	UCustomSurroundingsScannerComponent* Scanner = Airplane->FindComponentByClass<UCustomSurroundingsScannerComponent>();
	if (!Scanner)
	{
		Scanner = NewObject<UCustomSurroundingsScannerComponent>(Airplane, TEXT("StreetLights_CustomScanner"));
		if (!Scanner)
			return nullptr;

		Scanner->ScanRadiusMeters   = ScannerScanRadiusMeters;
		Scanner->GroundTraceChannel = CollisionChannel;
		Scanner->RegisterComponent();

		UE_LOG(LogUAV, Log, TEXT("StreetLightsManager: на %s ще не було UCustomSurroundingsScannerComponent — додав новий (ObjectsJson — лише заглушка, заповніть реальними будівлями)"),
			*Airplane->GetName());
	}

	OwnScannersByAirplane.Add(Airplane, Scanner);
	return Scanner;
}

// ─────────────────────────────────────────────────────────────────────────────
// Розміщення вогнів — з уже готового footprint сканера (реальні чотири кути, прив'язані до
// рельєфу), не з обчисленої чи семпльованої позиції.
// ─────────────────────────────────────────────────────────────────────────────

TArray<FVector> AStreetLightsManager::MakeRandomFootprintAround(const FString& Key, const FVector& CenterMeters) const
{
	// Потік, засіяний ObjectID: той самий об'єкт завжди дає той самий прямокутник (не "пливе"
	// між перезапусками сканера/сесіями), різні об'єкти — різні розміри й повороти.
	FRandomStream Random(GetTypeHash(Key));

	const float MinSize = FMath::Max(1.0f, FMath::Min(CesiumFootprintMinSizeMeters, CesiumFootprintMaxSizeMeters));
	const float MaxSize = FMath::Max(MinSize, CesiumFootprintMaxSizeMeters);
	const double HalfWidth = Random.FRandRange(MinSize, MaxSize) * 0.5;
	const double HalfDepth = Random.FRandRange(MinSize, MaxSize) * 0.5;
	const double YawRad = FMath::DegreesToRadians(Random.FRandRange(0.0f, 180.0f));

	const FVector AxisX(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.0);
	const FVector AxisY(-FMath::Sin(YawRad), FMath::Cos(YawRad), 0.0);

	// Обхід по периметру: (-,-) -> (+,-) -> (+,+) -> (-,+).
	return {
		CenterMeters - AxisX * HalfWidth - AxisY * HalfDepth,
		CenterMeters + AxisX * HalfWidth - AxisY * HalfDepth,
		CenterMeters + AxisX * HalfWidth + AxisY * HalfDepth,
		CenterMeters - AxisX * HalfWidth + AxisY * HalfDepth,
	};
}

bool AStreetLightsManager::BuildLightsForObject(const FString& Key, const TArray<FVector>& InCorners, FStreetLightBuilding& OutBuilding) const
{
	// Порядок кутів у "bbox" сканера (x_min -> x_max -> y_min -> y_max) не гарантує обхід по
	// периметру: залежно від об'єкта y_min/y_max можуть бути зі сходу чи із заходу, і тоді
	// з'єднання по черзі дає "метелика" (дві сторони перетинаються). Для чотирьох точок
	// із трьох можливих циклічних обходів справжній контур — той, що має найменший периметр
	// (обходи з перетином замінюють дві сторони діагоналями, які довші).
	TArray<FVector> Corners = InCorners;
	if (Corners.Num() == 4)
	{
		static const int32 Orders[3][4] = { {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3} };
		double BestPerimeter = TNumericLimits<double>::Max();
		const int32* BestOrder = Orders[0];
		for (const int32(&Order)[4] : Orders)
		{
			double Perimeter = 0.0;
			for (int32 i = 0; i < 4; ++i)
				Perimeter += FVector::Dist(InCorners[Order[i]], InCorners[Order[(i + 1) % 4]]);
			if (Perimeter < BestPerimeter)
			{
				BestPerimeter = Perimeter;
				BestOrder = Order;
			}
		}
		for (int32 i = 0; i < 4; ++i)
			Corners[i] = InCorners[BestOrder[i]];
	}

	OutBuilding.ObjectID = Key;
	OutBuilding.FootprintCornersWorldMeters = Corners;


	TArray<FVector> PerimeterPoints;
	for (int32 CornerIndex = 0; CornerIndex < Corners.Num(); ++CornerIndex)
	{
		const FVector& From = Corners[CornerIndex];
		const FVector& To   = Corners[(CornerIndex + 1) % Corners.Num()];
		const double EdgeLen = FVector::Dist(From, To);
		const int32 PointCount = FMath::Max(1, FMath::FloorToInt(EdgeLen / LightSpacingMeters));
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
			PerimeterPoints.Add(FMath::Lerp(From, To, (double)PointIndex / PointCount));
	}

	OutBuilding.LightPositionsWorldMeters.Reset(PerimeterPoints.Num());
	for (const FVector& Point : PerimeterPoints)
		OutBuilding.LightPositionsWorldMeters.Add(Point + FVector::UpVector * LightHeightMeters);

	return OutBuilding.LightPositionsWorldMeters.Num() > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// TrackedBuildingsMap — єдині дві операції, що його змінюють (той самий ObjectStorage-патерн,
// що в UCesiumSurroundingsScannerComponent/UCustomSurroundingsScannerComponent).
// ─────────────────────────────────────────────────────────────────────────────

void AStreetLightsManager::AddBuilding(const FString& Key, const FStreetLightBuilding& Building)
{
	TrackedBuildingsMap.Add(Key, Building);
	bLightPositionsDirty = true;
}

void AStreetLightsManager::RemoveBuilding(const FString& Key)
{
	TrackedBuildingsMap.Remove(Key);
	bLightPositionsDirty = true;
}

void AStreetLightsManager::RunValiditySweep(const TArray<FVector>& AirplaneLocationsCm)
{
	if (MaxTrackingDistanceMeters <= 0.0f)
		return; // 0 = вогні ніколи не прибираються

	if (AirplaneLocationsCm.Num() == 0)
		return; // жодного літака цього проходу — нічого не прибираємо (можливо, ще не заспавнили)

	const double MaxDistanceCm = MaxTrackingDistanceMeters * 100.0;

	TArray<FString> StaleKeys;
	for (const TPair<FString, FStreetLightBuilding>& Pair : TrackedBuildingsMap)
	{
		if (Pair.Value.FootprintCornersWorldMeters.Num() == 0)
			continue;

		const FVector BuildingCenterCm = Pair.Value.FootprintCornersWorldMeters[0] * 100.0;

		bool bNearAnyAirplane = false;
		for (const FVector& AirplaneLocationCm : AirplaneLocationsCm)
		{
			if (FVector::DistXY(BuildingCenterCm, AirplaneLocationCm) <= MaxDistanceCm)
			{
				bNearAnyAirplane = true;
				break;
			}
		}

		if (!bNearAnyAirplane)
			StaleKeys.Add(Pair.Key);
	}

	for (const FString& Key : StaleKeys)
		RemoveBuilding(Key);
}

void AStreetLightsManager::RebuildNiagaraArrays()
{
	TrackedBuildings.Reset();
	TrackedBuildings.Reserve(TrackedBuildingsMap.Num());

	TArray<FVector> FlatLightPositionsCm;
	FBox LightsBoundsCm(ForceInit);
	for (const TPair<FString, FStreetLightBuilding>& Pair : TrackedBuildingsMap)
	{
		TrackedBuildings.Add(Pair.Value);
		for (const FVector& PositionMeters : Pair.Value.LightPositionsWorldMeters)
		{
			FlatLightPositionsCm.Add(PositionMeters * 100.0);
			LightsBoundsCm += PositionMeters * 100.0;
		}
	}

	if (NiagaraComp)
	{
		// GPU-емітер не вміє рахувати bounds частинок сам: без фіксованих bounds система культиться
		// за крихітною коробкою навколо актора (±100 см) і вогні зникають навіть у полі зору.
		// Задаємо bounds по факту всіх вогнів (локально відносно компонента) з великим запасом —
		// frustum culling лишається, але тепер працює по реальній області вогнів.
		NiagaraComp->SetAllowScalability(false);
		if (LightsBoundsCm.IsValid)
		{
			const FVector Origin = NiagaraComp->GetComponentLocation();
			const FVector Pad(10000.0);
			NiagaraComp->SetSystemFixedBounds(FBox(LightsBoundsCm.Min - Origin - Pad, LightsBoundsCm.Max - Origin + Pad));
		}

		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComp, LightPositionsParameterName, FlatLightPositionsCm);
		NiagaraComp->SetIntParameter(TargetSpawnCountParameterName, FlatLightPositionsCm.Num());

		// SpawnBurst_Instantaneous спрацьовує лише раз за "цикл" емітера — Activate(true)
		// примусово перезапускає симуляцію (знищує старі частинки й одразу спавнить новий
		// burst на поточний TargetSpawnCount), тож масив завжди повністю й одразу
		// відображається, без поступового заповнення (як було з continuous SpawnRate) чи
		// випадкового "перетасовування" від безперервного respawn. Лише поки Brightness>0 —
		// інакше компонент має лишатися неактивним (SetBrightness(0) деактивував його свідомо).
		if (Brightness > 0.0f)
		{
			NiagaraComp->Activate(/*bReset=*/true);
		}
	}
}
