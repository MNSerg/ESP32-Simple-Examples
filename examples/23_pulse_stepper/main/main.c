/*
 * Пример 23. Длительность импульса решает, на сколько повернуть шаговик.
 *
 * На GPIO4 приходит импульс. Прерывание запоминает момент фронта и
 * спада через esp_timer_get_time() и кладёт ширину в очередь.
 * Задача сравнивает ширину с порогом THRESHOLD_US:
 *   импульс короче порога — SHORT_STEPS шагов в одну сторону,
 *   импульс не короче порога — LONG_STEPS шагов в другую.
 *
 * DRV8833 — это два моста, а не драйвер с входами STEP/DIR.
 * Четыре вывода AIN1, AIN2, BIN1, BIN2 сами перебирают фазы
 * биполярного двигателя. Пятипроводный униполярный 28BYJ-48
 * напрямую к этим мостам не подходит.
 *
 * SELF_TEST = 1 выдаёт с GPIO20 короткие и длинные импульсы.
 * Перемычка GPIO20 — GPIO4 позволяет проверить измерение без
 * генератора. Внешний сигнал требует SELF_TEST 0, иначе GPIO20
 * будет спорить с источником.
 */

#include <inttypes.h> /* PRId64 и PRIu32 печатают ширину импульса и число отброшенных событий. */

/*
 * Библиотеки:
 * driver/gpio.h — вход импульса, выходы мостов DRV8833, сон драйвера и nFAULT.
 * esp_log.h — печать ширины и того, сколько шагов выбрано. В прерывании лог запрещён.
 * esp_rom_sys.h — esp_rom_delay_us ждёт микросекунды между фазами двигателя.
 *   vTaskDelay здесь груб: один тик FreeRTOS — это 10 мс, а шаг длится 2 мс.
 * esp_timer.h — esp_timer_get_time в прерывании даёт время в микросекундах.
 *   Разница фронта и спада — ширина импульса.
 * freertos/FreeRTOS.h, queue.h, task.h — очередь из прерывания в задачу,
 *   которая уже крутит двигатель. В прерывании двигатель не шагают.
 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "step";

#define PULSE_GPIO GPIO_NUM_4
#define TEST_GPIO GPIO_NUM_20
#define AIN1_GPIO GPIO_NUM_7
#define AIN2_GPIO GPIO_NUM_10
#define BIN1_GPIO GPIO_NUM_1
#define BIN2_GPIO GPIO_NUM_3
#define NSLEEP_GPIO GPIO_NUM_21
#define NFAULT_GPIO GPIO_NUM_0

#define THRESHOLD_US 1000
#define SHORT_STEPS 32
#define LONG_STEPS 200
#define STEP_INTERVAL_US 2000
#define GLITCH_US 100
#define STUCK_US 500000
#define SELF_TEST 1

/*
 * Полный шаг, обе фазы включены.
 * Никогда не ставить 1 на оба входа одного моста: AIN1 и AIN2,
 * либо BIN1 и BIN2. Так выходы драйвера замыкаются друг на друга.
 * Индекс растёт — вал крутится в одну сторону. Поменять местами
 * провода одной обмотки — направление станет обратным.
 */
static const int STEP_SEQ[4][4] = {
    {1, 0, 1, 0},
    {0, 1, 1, 0},
    {0, 1, 0, 1},
    {1, 0, 0, 1},
};

static QueueHandle_t s_pulses;
static volatile uint32_t s_dropped;
static int64_t s_rise_us;
static bool s_level_high;
static int s_phase;

static void IRAM_ATTR pulse_isr(void *arg)
{
    (void)arg;
    /* Время с запуска чипа. Вызов можно делать из прерывания, ESP_LOGI — нельзя. */
    int64_t now = esp_timer_get_time();
    /* Уровень после фронта: 1 — импульс начался, 0 — импульс кончился. */
    int level = gpio_get_level(PULSE_GPIO);

    if (level == 1) {
        s_rise_us = now;
        s_level_high = true;
        return;
    }
    if (!s_level_high) {
        return;
    }
    s_level_high = false;

    int64_t width = now - s_rise_us;
    if (width < 0) {
        return;
    }
    BaseType_t wake = pdFALSE;
    /*
     * FromISR не ждёт свободного места. Если очередь полна, импульс теряется,
     * а счётчик потом напечатает задача. Третий аргумент сообщает, надо ли
     * сразу отдать процессор задаче двигателя.
     */
    if (xQueueSendFromISR(s_pulses, &width, &wake) != pdTRUE) {
        s_dropped++;
    }
    /* Если задача двигателя важнее текущей, переключиться на неё сразу. */
    portYIELD_FROM_ISR(wake);
}

static void coils_write(int ain1, int ain2, int bin1, int bin2)
{
    /*
     * Четыре входа DRV8833. Пара AIN — первая обмотка, пара BIN — вторая.
     * Единица на входе включает соответствующий выход моста.
     */
    gpio_set_level(AIN1_GPIO, ain1);
    gpio_set_level(AIN2_GPIO, ain2);
    gpio_set_level(BIN1_GPIO, bin1);
    gpio_set_level(BIN2_GPIO, bin2);
}

static void coils_off(void)
{
    coils_write(0, 0, 0, 0);
}

static void step_once(int direction)
{
    /*
     * direction равен +1 или -1. Прибавка 4 перед остатком не даёт отрицательному
     * индексу уйти в минус: в Си остаток от отрицательного числа тоже отрицательный.
     */
    s_phase = (s_phase + direction + 4) % 4;
    const int *row = STEP_SEQ[s_phase];
    coils_write(row[0], row[1], row[2], row[3]);
    esp_rom_delay_us(STEP_INTERVAL_US);
}

static void move_steps(int steps, int direction)
{
    if (gpio_get_level(NFAULT_GPIO) == 0) {
        ESP_LOGE(TAG, "nFAULT = 0, драйвер сообщает аварию, шаги пропущены");
        coils_off();
        return;
    }

    /* Единица на nSLEEP выводит драйвер из сна. Пока здесь ноль, выходы мостов выключены. */
    gpio_set_level(NSLEEP_GPIO, 1);
    for (int i = 0; i < steps; i++) {
        step_once(direction);
        if (gpio_get_level(NFAULT_GPIO) == 0) {
            ESP_LOGE(TAG, "nFAULT стал 0 на шаге %d, движение остановлено", i);
            break;
        }
    }
    /* После хода обмотки отпускаем: вал свободен, драйвер меньше греется. */
    coils_off();
}

static void stepper_task(void *arg)
{
    (void)arg;
    int64_t width = 0;
    uint32_t seen_drops = 0;

    while (1) {
        if (xQueueReceive(s_pulses, &width, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint32_t drops = s_dropped;
        if (drops != seen_drops) {
            ESP_LOGW(TAG, "очередь была полна, отброшено импульсов: %" PRIu32, drops - seen_drops);
            seen_drops = drops;
        }

        if (width < GLITCH_US) {
            ESP_LOGI(TAG, "импульс %" PRId64 " мкс короче %d, это помеха", width, GLITCH_US);
            continue;
        }
        if (width > STUCK_US) {
            ESP_LOGW(TAG, "импульс %" PRId64 " мкс длиннее %d мс, уровень застрял", width, STUCK_US / 1000);
            continue;
        }

        if (width >= THRESHOLD_US) {
            ESP_LOGI(TAG, "%" PRId64 " мкс >= %d, %d шагов вперёд", width, THRESHOLD_US, LONG_STEPS);
            move_steps(LONG_STEPS, +1);
        } else {
            ESP_LOGI(TAG, "%" PRId64 " мкс < %d, %d шагов назад", width, THRESHOLD_US, SHORT_STEPS);
            move_steps(SHORT_STEPS, -1);
        }
    }
}

#if SELF_TEST
static void pulse_out(uint32_t width_us)
{
    gpio_set_level(TEST_GPIO, 1); /* Фронт, с которого измерение начинает считать ширину. */
    esp_rom_delay_us(width_us);   /* Держать единицу ровно заданное число микросекунд. */
    gpio_set_level(TEST_GPIO, 0); /* Спад. Прерывание на GPIO4 посчитает разницу. */
}

static void generator_task(void *arg)
{
    (void)arg;
    bool long_pulse = false;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2500));
        uint32_t width = long_pulse ? 1500 : 400;
        long_pulse = !long_pulse;
        pulse_out(width);
        ESP_LOGI(TAG, "тест: на GPIO%d выдан импульс %" PRIu32 " мкс", TEST_GPIO, width);
    }
}
#endif

static void output_low(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(pin, 0);
}

void app_main(void)
{
    /* Сначала обмотки выключены, и только потом драйвер выходит из сна. */
    output_low(AIN1_GPIO);
    output_low(AIN2_GPIO);
    output_low(BIN1_GPIO);
    output_low(BIN2_GPIO);
    output_low(NSLEEP_GPIO);
    /* Сначала все входы мостов уже в нуле, и только потом драйвер выходит из сна. */
    gpio_set_level(NSLEEP_GPIO, 1);

    gpio_config_t fault = {
        .pin_bit_mask = 1ULL << NFAULT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&fault));

    gpio_config_t pulse = {
        .pin_bit_mask = 1ULL << PULSE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&pulse));

    s_pulses = xQueueCreate(4, sizeof(int64_t));
    if (s_pulses == NULL) {
        ESP_LOGE(TAG, "не удалось создать очередь импульсов");
        return;
    }

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PULSE_GPIO, pulse_isr, NULL));

    if (xTaskCreate(stepper_task, "stepper", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "не удалось создать задачу двигателя");
        return;
    }

#if SELF_TEST
    output_low(TEST_GPIO);
    if (xTaskCreate(generator_task, "pulse", 2048, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "не удалось создать генератор импульсов");
    }
    ESP_LOGI(TAG, "самопроверка: соедините GPIO%d и GPIO%d", TEST_GPIO, PULSE_GPIO);
#else
    ESP_LOGI(TAG, "внешний импульс на GPIO%d, порог %d мкс", PULSE_GPIO, THRESHOLD_US);
#endif
}
