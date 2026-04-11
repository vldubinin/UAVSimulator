#pragma once

#include "CoreMinimal.h"
#include "UAVSimulator/Entity/PolarRow.h"

/**
 * Low-level I/O and external-process helpers for the aerodynamic analysis toolchain.
 * Handles file system operations, polar file parsing, and Python/XFoil/OpenVSP execution.
 */
class UAVSIMULATOR_API AerodynamicToolRunner
{
public:
	/**
	 * Повертає (і створює за необхідності) тимчасову робочу директорію для виводу інструментів.
	 * Шлях будується так, щоб його довжина не перевищувала 45 символів (обмеження зовнішніх утиліт).
	 * @return Абсолютний шлях до директорії "us_tmp".
	 */
	static FString GetOrCreateWorkDir();

	/**
	 * Видаляє вміст тимчасової робочої директорії разом з самою директорією.
	 * @return true якщо видалення пройшло успішно.
	 */
	static bool CleanWorkDir();

	/**
	 * Перетворює шлях відносно проекту на абсолютний шлях для зовнішніх програм.
	 * @param Path — відносний шлях.
	 * @return Абсолютний шлях.
	 */
	static FString ToAbsolutePath(FString Path);

	/**
	 * Копіює файл у тимчасову робочу директорію під заданим ім'ям.
	 * @param SourcePath — повний шлях до джерела.
	 * @param Name       — ім'я файлу у директорії призначення.
	 * @return Абсолютний шлях до скопійованого файлу.
	 */
	static FString CopyToWorkDir(FString SourcePath, FString Name);

	/**
	 * Серіалізує мапу полярних характеристик у текстовий .dat файл у робочій директорії.
	 * Формат: заголовок + рядки "AoA CL CD CM", відсортовані за кутом атаки.
	 * @param Polar — мапа {кут атаки → FPolarRow}.
	 * @return Абсолютний шлях до створеного файлу "airfoil.dat".
	 */
	static FString SavePolarFile(TMap<float, FPolarRow> Polar);

	/**
	 * Виконує Python-скрипт через UE PythonScriptPlugin та виводить лог у LogUAV.
	 * @param Command — рядок команди (шлях до скрипту + аргументи через пробіл).
	 * @return true якщо виконання завершилось без помилок.
	 */
	static bool RunPythonScript(FString Command);

	/**
	 * Парсить текстовий файл полярних характеристик з роздільником-пробілом.
	 * Рядки, що не відповідають шаблону числових даних, пропускаються.
	 * @param Path   — абсолютний шлях до файлу.
	 * @param AoAIdx — індекс стовпця кута атаки (з нуля).
	 * @param ClIdx  — індекс стовпця CL.
	 * @param CdIdx  — індекс стовпця CD.
	 * @param CmIdx  — індекс стовпця CM.
	 * @return Мапа {кут атаки → FPolarRow}; порожня якщо файл не знайдено.
	 */
	static TMap<float, FPolarRow> ParsePolarFile(FString Path, int32 AoAIdx, int32 ClIdx, int32 CdIdx, int32 CmIdx);

	/**
	 * Визначає ім'я папки ЛА з пакетного шляху класу UObject.
	 * Очікує формат "/Game/Airplane/<FolderName>/...".
	 * @param ContextObject — будь-який UObject, клас якого розташований у папці ЛА.
	 * @return Ім'я папки ЛА (перший сегмент після "/Game/Airplane/").
	 */
	static FString GetOwnerFolderName(UObject* ContextObject);
};
