import json
import glob
from collections import defaultdict

import json
import os

def find_missing_flaps(filepath, step=1.0):
    """
    Знаходить пропущені значення flap у файлі даних.
    
    :param filepath: Шлях до файлу (наприклад, 'history_naca0009_0.8_-45.0_45.0.data')
    :param step: Очікуваний крок між значеннями flap (за замовчуванням 1.0)
    :return: Список пропущених значень
    """
    if not os.path.exists(filepath):
        print(f"Файл {filepath} не знайдено.")
        return []

    actual_flaps = set()
    
    # Зчитуємо дані з файлу
    with open(filepath, 'r', encoding='utf-8') as file:
        for line in file:
            line = line.strip()
            if not line:
                continue
                
            try:
                # Парсимо кожен рядок як JSON
                data = json.loads(line)
                
                # Витягуємо значення flap
                if 'condition' in data and 'flap' in data['condition']:
                    actual_flaps.add(data['condition']['flap'])
            except json.JSONDecodeError:
                continue
                
    if not actual_flaps:
        print(f"У файлі {filepath} не знайдено значень flap.")
        return []

    # Знаходимо мінімальне та максимальне значення, щоб побудувати очікуваний ряд
    min_flap = min(actual_flaps)
    max_flap = max(actual_flaps)
    
    # Створюємо список очікуваних значень з урахуванням кроку
    expected_flaps = set()
    current_flap = min_flap
    while current_flap <= max_flap:
        expected_flaps.add(round(current_flap, 2)) # Округлюємо для уникнення проблем з float
        current_flap += step

    # Знаходимо пропущені значення (округлюємо фактичні для коректного порівняння)
    rounded_actual_flaps = {round(f, 2) for f in actual_flaps}
    missing_flaps = sorted(list(expected_flaps - rounded_actual_flaps))
    
    return missing_flaps

def analyze_files():
    # Шукаємо всі файли з розширенням .data у поточній директорії
    files = glob.glob("*.data")
    
    if not files:
        print("Файли не знайдені.")
        return

    for filepath in files:
        # Використовуємо defaultdict та set, щоб зберігати унікальні значення AoA
        flap_data = defaultdict(set)
        
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                for line in f:
                    if not line.strip(): 
                        continue
                        
                    data = json.loads(line)
                    
                    # Перевіряємо, чи містить рядок ключ 'condition'
                    if 'condition' in data:
                        flap = data['condition']['flap']
                        aoa = data['condition']['AoA']
                        flap_data[flap].add(aoa)
                        
            print(f"\n--- Аналіз файлу: {filepath} ---")
            
            # Відбираємо закрилки, для яких зібрано менше 10 унікальних AoA
            insufficient = {flap: len(aoas) for flap, aoas in sorted(flap_data.items()) if len(aoas) < 10}
            
            if not insufficient:
                print("✅ Для всіх кутів закрилків є 10 або більше значень AoA.")
            else:
                print("⚠️ Кути закрилків з менш ніж 10 значеннями AoA:")
                for flap, count in insufficient.items():
                    print(f"  Закрилок {flap}°: {count} значень AoA")
            missing = find_missing_flaps(filepath, step=1.0)
        
            if missing:
                print(f"⚠ Знайдено пропущені значення flap ({len(missing)} шт.):")
                print(missing)
            else:
                print("✅ Пропущених значень не виявлено (або файл містить лише одне значення).")

            
        except FileNotFoundError:
            print(f"Помилка: Файл {filepath} не знайдено.")
        except json.JSONDecodeError:
            print(f"Помилка: Неможливо прочитати JSON у файлі {filepath}.")

if __name__ == "__main__":
    analyze_files()