/*
 * Пример 18. Лёгкий сон.
 *
 * В лёгком сне процессор останавливается, но оперативная память
 * остаётся под питанием. Функция esp_light_sleep_start() возвращает
 * управление на следующую строку, как только чип проснулся.
 * Глобальные переменные целы. Перезагрузки нет.
 *
 * Ток заметно ниже активного режима (порядка сотен микроампер без
 * радио; точное значение — в даташите), но выше глубокого сна:
 * память ведь надо на чём-то держать.
 *
 * Пробуждение здесь из двух источников сразу:
 *   таймер на 8 секунд;
 *   низкий уровень на GPIO9 — та же кнопка, что в проекте с дисплеем
 *   и кнопка BOOT на типичной плате.
 * Для лёгкого сна, пока блок GPIO под питанием, годится любой вывод.
 * Ограничение «только GPIO0…5» относится к глубокому сну, не к этому.
 *
 * Важная мелочь: источник GPIO в лёгком сне ждёт уровень, не фронт.
 * Если войти в сон, когда кнопка уже зажата (на выводе уже 0),
 * чип проснётся немедленно. Входите в сон при отпущенной кнопке.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"

#define BUTTON_GPIO GPIO_NUM_9
#define SLEEP_SECONDS 8

static const char *TAG = "light_sleep";

static const char *cause_text(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
        return "таймер";
    case ESP_SLEEP_WAKEUP_GPIO:
        return "кнопка GPIO9";
    default:
        return "другой источник";
    }
}

static void button_setup(void)
{
    gpio_config_t button = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));

    /*
     * Для пробуждения допустимы только уровни HIGH и LOW, не фронты.
     * LOW совпадает с кнопкой, которая замыкает вывод на землю.
     */
    ESP_ERROR_CHECK(gpio_wakeup_enable(BUTTON_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL));
}

void app_main(void)
{
    int sleep_index = 0;
    button_setup();
    ESP_LOGI(TAG, "лёгкий сон на %d с или по кнопке GPIO%d", SLEEP_SECONDS, BUTTON_GPIO);

    while (true) {
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            ESP_LOGW(TAG, "кнопка зажата, жду отпускания. Иначе сон сразу кончится");
            while (gpio_get_level(BUTTON_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }

        sleep_index++;
        ESP_LOGI(TAG, "сон %d. Память и переменные сохранятся", sleep_index);
        vTaskDelay(pdMS_TO_TICKS(50));

        ESP_ERROR_CHECK(esp_light_sleep_start());

        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        ESP_LOGI(TAG, "проснулись: %s. sleep_index по-прежнему %d",
                 cause_text(cause), sleep_index);
        /* Короткая пауза, чтобы лог успел уйти и кнопка успела отпуститься. */
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
