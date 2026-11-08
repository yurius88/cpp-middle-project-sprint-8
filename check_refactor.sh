#!/bin/bash

# Путь к папке с тестовыми файлами и эталонами
TEST_DATA_FLDR="${PWD}/tests/tests_data"
TMP_FLDR="$TEST_DATA_FLDR/tmp"

# Список тестовых файлов и эталонов
TEST_FILES=("test1.cpp" "test2.cpp" "test3.cpp")
REF_FILES=("$TEST_DATA_FLDR/test1_ref.cpp" "$TEST_DATA_FLDR/test2_ref.cpp" "$TEST_DATA_FLDR/test3_ref.cpp")

# Путь к утилите студента
TOOL="./build/refactor_tool"

# Массивы для хранения результатов
declare -a TEST_RESULTS
declare -a TEST_DETAILS

# Проверяем, существует ли утилита
if [ ! -f "$TOOL" ]; then
    echo "Ошибка: Утилита $TOOL не найдена. Убедитесь, что она собрана."
    exit 1
fi

# Проверяем, существует ли папка с тестовыми данными
if [ ! -d "$TEST_DATA_FLDR" ]; then
    echo "Ошибка: Папка $TEST_DATA_FLDR не найдена."
    exit 1
fi

mkdir "$TMP_FLDR"
for i in "${!TEST_FILES[@]}"; do
    TEST_FILE="$TEST_DATA_FLDR/${TEST_FILES[$i]}"
    REF_FILE="${REF_FILES[$i]}"
    TMP_FILE="$TMP_FLDR/temp_${TEST_FILES[$i]}"
    
    # Копируем тестовый файл для рефакторинга
    cp "$TEST_FILE" "$TMP_FILE"
    
    # # Запускаем инструмент
    $TOOL "$TMP_FILE" --
        
    # Проверяем, успешно ли выполнился инструмент
    if [ $? -ne 0 ]; then
        TEST_RESULTS[$i]="Ошибка: Утилита завершилась с ошибкой для $TEST_FILE."
        TEST_DETAILS[$i]=""
        # Не удаляем временный файл при ошибке
        continue
    fi
   
    # Сравниваем с эталоном (игнорируем пробелы/пустые строки с помощью diff -w -B)
    DIFF_OUTPUT=$(diff -w -B "$TMP_FILE" "$REF_FILE")
    if [ $? -eq 0 ]; then
        TEST_RESULTS[$i]="Тест $TEST_FILE пройден!"
        TEST_DETAILS[$i]=""
        # Удаляем временный файл, если тест пройден
        rm "$TMP_FILE"
    else
        TEST_RESULTS[$i]="Тест $TEST_FILE провален!"
        TEST_DETAILS[$i]="Различия:\n$DIFF_OUTPUT"
        # Не удаляем временный файл, если тест провален
    fi
done

# Вывод результатов в конце
echo "===== Результаты тестов ====="
for i in "${!TEST_FILES[@]}"; do
    echo "${TEST_RESULTS[$i]}"
    if [ -n "${TEST_DETAILS[$i]}" ]; then
        echo -e "${TEST_DETAILS[$i]}"
    fi
done