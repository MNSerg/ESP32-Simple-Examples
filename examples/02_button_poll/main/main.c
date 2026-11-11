/*
 * Пример 02. Кнопка опросом, с антидребезгом.
 *
 * Опрос (polling) — самый прямой способ прочитать кнопку: программа
 * сама регулярно спрашивает «какой сейчас уровень на выводе?».
 * Пока кнопка не нажата, задача просто спит по 10 мс и проверяет снова.
 *
 * У механической кнопки контакты дребезжат: за одно нажатие уровень
 * несколько раз успевает смениться с 0 на 1 и обратно. Если считать
 * каждое такое изменение отдельным нажатием, одно касание превратится
 * в пачку срабатываний. Поэтому после первого обнаружения программа
 * ждёт 30 мс и проверяет уровень ещё раз. Это и есть антидребезг.
 *
 * Кнопка в исходном проекте с дисплеем подключена к GPIO9 и замыкает
 * вывод на землю. Здесь используется та же схема.
 */

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* Кнопка BOOT на большинстве плат ESP32-C3 тоже сидит на GPIO9. */
#define BUTTON_GPIO GPIO_NUM_9

/* Светодиод переключается по каждому подтверждённому нажатию. */
#define LED_GPIO GPIO_NUM_8
#define LED_ON_LEVEL 0

/* Сколько ждать, пока контакты кнопки перестанут дребезжать. */
#define DEBOUNCE_MS 30

static const char *TAG = "button_poll";

static void led_set(bool on)
{
    int level = on ? LED_ON_LEVEL : (1 - LED_ON_LEVEL);
    gpio_set_level(LED_GPIO, level);
}

/* Кнопка замыкает вывод на землю, поэтому «нажато» — это уровень 0. */
static bool button_is_down(void)
{
    return gpio_get_level(BUTTON_GPIO) == 0;
}

void app_main(void)
{
    ESP_LOGI(TAG, "кнопка GPIO%d, светодиод GPIO%d", BUTTON_GPIO, LED_GPIO);

    gpio_config_t button = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        /*
         * Внутренняя подтяжка к питанию. Пока кнопку не трогают,
         * на входе стабильная 1. Нажатие притягивает вход к земле (0).
         * Без подтяжки ненажатый вход «висит в воздухе» и читается случайно.
         */
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));

    gpio_config_t led = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led));
    led_set(false);

    int presses = 0;
    bool led_on = false;

    while (true) {
        if (button_is_down()) {
            /* Первое чтение могло попасть в дребезг. Ждём и читаем снова. */
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (button_is_down()) {
                presses++;
                led_on = !led_on;
                led_set(led_on);
                ESP_LOGI(TAG, "нажатие %d, светодиод %s",
                         presses, led_on ? "включён" : "выключен");

                /* Ждём отпускания, иначе одно удержание даст много нажатий. */
                while (button_is_down()) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                /* И короткий дребезг на отпускании тоже пропускаем. */
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
