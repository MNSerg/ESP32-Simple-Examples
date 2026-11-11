/*
 * Пример 01. Мигание светодиодом через GPIO.
 *
 * GPIO (General Purpose Input/Output) — универсальный вывод микроконтроллера.
 * Его можно сделать входом (читать кнопку) или выходом (включать светодиод).
 *
 * Здесь вывод работает как выход: программа по очереди ставит на нём
 * логический 0 и логическую 1. Светодиод, подключённый к этому выводу,
 * будет мигать.
 *
 * Чип: ESP32-C3. Среда: ESP-IDF 6.1, язык C, планировщик FreeRTOS.
 * Точка входа программы — функция app_main(), а не main().
 * Функцию main() вызывает сам ESP-IDF: он поднимает FreeRTOS и только
 * потом вызывает app_main() уже внутри задачи «main».
 */

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

/*
 * К какому выводу подключён светодиод.
 *
 * GPIO8 — частый выбор на платах ESP32-C3 SuperMini: там стоит
 * синий светодиод. На DevKit со «адресным» RGB-светодиодом (WS2812)
 * этот пример лампочку не зажжёт — нужен обычный светодиод.
 *
 * Поменяйте номер, если ваш светодиод сидит на другом выводе.
 * Не используйте GPIO18 и GPIO19: на C3 это линии USB.
 */
#define LED_GPIO GPIO_NUM_8

/*
 * Какой уровень включает светодиод.
 * 0 — светодиод горит, когда на выводе низкий уровень. Так сделано
 *     на многих платах: анод светодиода сидит на 3.3 В, катод — на GPIO.
 * 1 — светодиод горит, когда на выводе высокий уровень
 *     (анод к GPIO через резистор, катод на землю).
 */
#define LED_ON_LEVEL 0

static const char *TAG = "gpio_blink";

/* Включить или выключить светодиод, не думая о полярности платы. */
static void led_set(bool on)
{
    int level = on ? LED_ON_LEVEL : (1 - LED_ON_LEVEL);
    gpio_set_level(LED_GPIO, level);
}

void app_main(void)
{
    ESP_LOGI(TAG, "старт. Светодиод на GPIO%d, уровень включения = %d",
             LED_GPIO, LED_ON_LEVEL);

    /*
     * gpio_config() настраивает сразу несколько свойств вывода.
     * pin_bit_mask — битовая маска: бит N соответствует GPIOn.
     * Сдвиг 1ULL (unsigned long long) обязателен: номера GPIO бывают
     * больше 31, и маска должна быть 64-битной.
     */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    bool on = false;
    while (true) {
        on = !on;
        led_set(on);
        ESP_LOGI(TAG, "светодиод %s", on ? "включён" : "выключен");

        /*
         * vTaskDelay уступает процессор другим задачам FreeRTOS.
         * Аргумент — не миллисекунды, а «тики» планировщика.
         * pdMS_TO_TICKS переводит миллисекунды в тики.
         *
         * Пустой цикл while без задержки здесь был бы ошибкой:
         * задача main крутилась бы без остановки и не давала
         * работать системным задачам.
         */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
