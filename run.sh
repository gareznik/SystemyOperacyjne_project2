#!/bin/bash

# ==========================================
# НАСТРОЙКИ
# ==========================================
ITERATIONS=5  # Сколько раз прогонять каждый сценарий (для точности среднего)
APP="./simulation"

# Компиляция
echo "Компиляция проекта..."
g++ -o simulation main.cpp -lncurses -lpthread
if [ $? -ne 0 ]; then echo "Ошибка компиляции!"; exit 1; fi

# ==========================================
# ФУНКЦИЯ ТЕСТИРОВАНИЯ
# ==========================================
run_scenario() {
    TEST_NAME=$1
    DURATION=$2
    shift 2
    INPUT_DATA="$@"

    echo "----------------------------------------------------------------"
    echo "Запуск сценария: '$TEST_NAME' ($ITERATIONS раз по ${DURATION}сек)..."
    
    # Инициализация сумм
    S_HALL_ORD=0; S_HALL_SRV=0; S_HALL_REJ=0
    S_TAKE_ORD=0; S_TAKE_SRV=0; S_TAKE_REJ=0
    S_CONS_VEG=0; S_CONS_MEAT=0; S_CONS_BREAD=0; S_CONS_DISP=0
    S_WASHED=0
    
    mkdir -p logs

    for (( i=1; i<=ITERATIONS; i++ ))
    do
        LOG_FILE="logs/${TEST_NAME// /_}_run_${i}.log"
        
        # Запуск одной симуляции
        timeout --signal=SIGINT ${DURATION}s $APP > "$LOG_FILE" 2>&1 << EOF
$INPUT_DATA
EOF
        
        # Парсинг результатов (с флагом -a для бинарных файлов)
        # 1. RUCH I ZAMOWIENIA (SALA / WYNOS)
        # SCENARIO: SALA | 10 | 10 | 0
        ROW_HALL=$(grep -a "^SALA " "$LOG_FILE")
        ROW_TAKE=$(grep -a "^WYNOS " "$LOG_FILE")
        
        HALL_ORD=$(echo "$ROW_HALL" | awk '{print $3}')
        HALL_SRV=$(echo "$ROW_HALL" | awk '{print $5}')
        HALL_REJ=$(echo "$ROW_HALL" | awk '{print $7}')
        
        TAKE_ORD=$(echo "$ROW_TAKE" | awk '{print $3}')
        TAKE_SRV=$(echo "$ROW_TAKE" | awk '{print $5}')
        TAKE_REJ=$(echo "$ROW_TAKE" | awk '{print $7}')
        
        # 2. ZUZYCIE PRODUKTOW (RAZEM term is the last column)
        # Warzywa | ... | ... | TOTAL
        CONS_VEG=$(grep -a "^Warzywa " "$LOG_FILE" | awk '{print $7}')
        CONS_MEAT=$(grep -a "^Mieso " "$LOG_FILE" | awk '{print $7}')
        CONS_BREAD=$(grep -a "^Chleb " "$LOG_FILE" | awk '{print $7}')
        CONS_DISP=$(grep -a "^Jednorazowe " "$LOG_FILE" | awk '{print $7}')
        
        # 3. KITCHEN
        WASHED=$(grep -a "Lacznie umyto sztuccow:" "$LOG_FILE" | awk '{print $NF}')

        # Накопление (с защитой от пустых строк если grep не нашел)
        S_HALL_ORD=$((S_HALL_ORD + ${HALL_ORD:-0}))
        S_HALL_SRV=$((S_HALL_SRV + ${HALL_SRV:-0}))
        S_HALL_REJ=$((S_HALL_REJ + ${HALL_REJ:-0}))
        
        S_TAKE_ORD=$((S_TAKE_ORD + ${TAKE_ORD:-0}))
        S_TAKE_SRV=$((S_TAKE_SRV + ${TAKE_SRV:-0}))
        S_TAKE_REJ=$((S_TAKE_REJ + ${TAKE_REJ:-0}))
        
        S_CONS_VEG=$((S_CONS_VEG + ${CONS_VEG:-0}))
        S_CONS_MEAT=$((S_CONS_MEAT + ${CONS_MEAT:-0}))
        S_CONS_BREAD=$((S_CONS_BREAD + ${CONS_BREAD:-0}))
        S_CONS_DISP=$((S_CONS_DISP + ${CONS_DISP:-0}))
        
        S_WASHED=$((S_WASHED + ${WASHED:-0}))
        
        echo -n "."
    done
    echo "" 

    # Расчет средних
    AVG_HALL_ORD=$((S_HALL_ORD / ITERATIONS))
    AVG_HALL_SRV=$((S_HALL_SRV / ITERATIONS))
    AVG_HALL_REJ=$((S_HALL_REJ / ITERATIONS))
    
    AVG_TAKE_ORD=$((S_TAKE_ORD / ITERATIONS))
    AVG_TAKE_SRV=$((S_TAKE_SRV / ITERATIONS))
    AVG_TAKE_REJ=$((S_TAKE_REJ / ITERATIONS))
    
    AVG_CONS_VEG=$((S_CONS_VEG / ITERATIONS))
    AVG_CONS_MEAT=$((S_CONS_MEAT / ITERATIONS))
    AVG_CONS_BREAD=$((S_CONS_BREAD / ITERATIONS))
    AVG_CONS_DISP=$((S_CONS_DISP / ITERATIONS))
    
    AVG_WASHED=$((S_WASHED / ITERATIONS))

    # Вывод красивой таблицы (Средние значения)
    echo "================================================================"
    echo "ИТОГОВЫЕ СРЕДНИЕ ПО '$TEST_NAME':"
    echo "----------------------------------------------------------------"
    printf "%-15s | %-10s | %-10s | %s\n" "Typ" "Zamowien" "Ludzi" "Odrzucono"
    echo "----------------------------------------------------------------"
    printf "%-15s | %-10d | %-10d | %d\n" "SALA" $AVG_HALL_ORD $AVG_HALL_SRV $AVG_HALL_REJ
    printf "%-15s | %-10d | %-10d | %d\n" "WYNOS" $AVG_TAKE_ORD $AVG_TAKE_SRV $AVG_TAKE_REJ
    echo "----------------------------------------------------------------"
    printf "%-15s | %-10d | %-10d | %d\n" "SUMA" $((AVG_HALL_ORD + AVG_TAKE_ORD)) $((AVG_HALL_SRV + AVG_TAKE_SRV)) $((AVG_HALL_REJ + AVG_TAKE_REJ))
    echo ""
    echo "ПОТРЕБЛЕНИЕ (Среднее):"
    echo "Warzywa: $AVG_CONS_VEG | Mieso: $AVG_CONS_MEAT | Chleb: $AVG_CONS_BREAD | 1-razowe: $AVG_CONS_DISP"
    echo "КУХНЯ:"
    echo "Umyto sztuccow: $AVG_WASHED"
    echo "================================================================"
}

# ==========================================
# СЦЕНАРИИ ТЕСТИРОВАНИЯ
# ==========================================
# ИНСТРУКЦИЯ ПО ИЗМЕНЕНИЮ ЦИФР:
# -----------------------------
# Строка 1: Столы (2-местные)  Столы (4-местные)  Столы (6-местные)
# Строка 2: Овощи  Мясо  Хлеб  Одноразовые_приборы
# Строка 3: Вилки  Ножи  Ложки
# Строка 4: Режим_доставки(1=Fix,2=Smart)  Время_доставки(сек)  Время_мойки(сек)  Мин_появление(сек)  Макс_появление(сек)
# Строка 5: Шанс_на_вынос(%)

# --- СЦЕНАРИЙ 1: ОБЫЧНЫЙ ДЕНЬ ---
run_scenario "Normal Day" 30 \
    6 5 2 \
    50 50 50 50 \
    20 20 20 \
    2 2 0.5 0.2 0.5 \
    30

# --- СЦЕНАРИЙ 2: ДЕФИЦИТ ПРИБОРОВ ---
# Здесь мы ставим всего по 2 вилки, ножа и ложки.
# Мойка медленная (1.0 сек). Ожидаем много отказов.
run_scenario "Cutlery Crisis" 30 \
    6 5 2 \
    50 50 50 50 \
    2 2 2 \
    2 2 1.0 0.2 0.5 \
    30

# --- СЦЕНАРИЙ 3: ТОЛЬКО ВЫНОС (FAST FOOD) ---
# Мало столов (по 1), но много одноразовых приборов (200).
# Шанс выноса 90%. Клиенты идут очень быстро (0.1 сек).
run_scenario "Fast Food Takeout" 30 \
    1 1 1 \
    50 50 50 200 \
    10 10 10 \
    2 2 0.5 0.1 0.2 \
    90

# --- СЦЕНАРИЙ 4: ПЕРЕБОИ С ПОСТАВКАМИ ---
# Режим доставки 1 (глупый), доставка едет очень долго (8 сек).
# Склад маленький (по 10 продуктов). Ожидаем голод.
run_scenario "Supply Issues" 30 \
    5 5 5 \
    10 10 10 20 \
    20 20 20 \
    1 8 0.5 0.2 0.5 \
    30

# Чистка временных файлов

echo "----------------------------------------------------------------"
echo "Все тесты завершены."