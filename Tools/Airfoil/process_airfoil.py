from airfoilprep import Airfoil
import os

# --- Налаштування ---
input_filename = 'polar.dat' # Вкажіть ім'я вашого вхідного файлу
output_filename = 'polar_result.dat' # Ім'я файлу для збереження результату
cd_max = 1.25 # Ключовий параметр для методу Вітерни

# Перевірка, чи існує вхідний файл
if not os.path.exists(input_filename):
    print(f"Помилка: Вхідний файл '{input_filename}' не знайдено!")
else:
    print(f"1. Завантаження даних з файлу: {input_filename}")
    # Ініціалізація об'єкта Airfoil з вашого файлу
    airfoil = Airfoil.initFromAerodynFile(input_filename)

    print("2. Виконання екстраполяції на 360 градусів...")
    # Виконання екстраполяції за методом Вітерни
    airfoil_extrapolated = airfoil.extrapolate(cd_max)

    print(f"3. Збереження результатів у файл: {output_filename}")
    # Запис результатів у новий файл
    airfoil_extrapolated.writeToAerodynFile(output_filename)

    print("\n✅ Роботу завершено успішно!")