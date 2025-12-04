@echo off
setlocal enabledelayedexpansion

:: --- Налаштування ---
:: Вхідний файл AeroDyn
set "inputFile=polar.dat"
:: Вихідний файл CSV
set "outputFile=polar.csv"
:: Кількість рядків заголовка для пропуску
set "skipLines=13"
:: -----------------

:: Перевірка наявності вхідного файлу
if not exist "%inputFile%" (
    echo [ERROR] File not found: %inputFile%
    echo Please make sure the script is in the same directory as the data file.
    pause
    exit /b 1
)

echo Converting %inputFile% to %outputFile%...

:: Створення вихідного файлу та запис заголовка CSV
echo AOA,CL,CD,CM > "%outputFile%"

:: Читання вхідного файлу, пропуск заголовка та обробка рядків
:: Використовуємо 'tokens=1-4' для вилучення чотирьох стовпців даних.
:: Стандартний роздільник (пробіл/табуляція) працює для цього формату.
for /f "skip=%skipLines% tokens=1-4" %%a in ('type "%inputFile%"') do (
    
    :: Перевірка на маркер кінця файлу "EOT"
    if "%%a"=="EOT" (
        goto :end_loop
    )
    
    :: Запис даних у файл CSV, розділених комами
    echo %%a,%%b,%%c,%%d >> "%outputFile%"
)

:end_loop
echo.
echo Conversion successful!
echo Output saved to: %outputFile%
echo.

endlocal
