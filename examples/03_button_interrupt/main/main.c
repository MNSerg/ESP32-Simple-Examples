/*
 * Пример 03. Кнопка через прерывание GPIO и очередь FreeRTOS.
 *
 * Прерывание — это аппаратный сигнал «на выводе что-то произошло».
 * Процессор бросает текущую работу, выполняет короткую функцию-обработчик
 * и возвращается назад. Основная программа в это время может спать
 * в ожидании события, а не опрашивать кнопку в цикле.
 *
 * В обработчике прерывания нельзя делать почти ничего долгого:
 * нельзя вызывать printf, ESP_LOG, vTaskDelay и большинство драйверов.
 * Разрешены только операции, в имени которых есть FromISR.
 * Поэтому обработчик лишь кладёт номер вывода в очередь. Разбором
 * события занимается обычная задача — там уже можно и ждать, и печатать.
 *
 * Это тот же приём, что в исходном проекте с дисплеем (isr_handler +
 * button_task), только здесь он разобран по шагам и дополнен антидребезгом.
 */

#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BUTTON_GPIO GPIO_NUM_9
#define LED_GPIO GPIO_NUM_8
#define LED_ON_LEVEL 0
#define DEBOUNCE_MS 40

static const char *TAG = "button_isr";

/*
 * Очередь — почтовый ящик между прерыванием и задачей.
 * Элемент очереди: номер GPIO, на котором случилось событие.
 * Одной очереди хватает и на несколько кнопок.
 */
static QueueHandle_t s_button_queue;

static void led_set(bool on)
{
    int level = on ? LED_ON_LEVEL : (1 - LED_ON_LEVEL);
    gpio_set_level(LED_GPIO, level);
}

/*
 * IRAM_ATTR кладёт функцию во внутреннюю RAM. Обработчик GPIO должен
 * быть доступен даже в те моменты, когда flash-кэш выключен.
 * Из этой функции виден только аргумент, который мы сами передали
 * при регистрации, — номер вывода.
 */
static void IRAM_ATTR button_isr(void *arg)
{
    uint32_t gpio = (uint32_t)arg;
    /*
     * Третий аргумент — флаг «разблокировать более приоритетную задачу».
     * Нам он не нужен: NULL означает «не сообщай».
     * xQueueSendFromISR не ждёт свободного места. Если очередь полна,
     * событие просто теряется. Для кнопки это нормально: дребезг
     * и так создаёт лишние события.
     */
    xQueueSendFromISR(s_button_queue, &gpio, NULL);
}

static void button_task(void *arg)
{
    (void)arg;
    uint32_t gpio = 0;
    int presses = 0;
    bool led_on = false;

    while (true) {
        /*
         * portMAX_DELAY — ждать сколько угодно. Задача спит и не занимает
         * процессор, пока прерывание не положит номер вывода в очередь.
         */
        if (xQueueReceive(s_button_queue, &gpio, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Событие пришло. Даём контактам успокоиться и проверяем уровень. */
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            presses++;
            led_on = !led_on;
            led_set(led_on);
            ESP_LOGI(TAG, "GPIO%" PRIu32 " нажатие %d, светодиод %s",
                     gpio, presses, led_on ? "включён" : "выключен");

            while (gpio_get_level(BUTTON_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        /* Выбрасываем события, которые дребезг успел положить в очередь. */
        while (xQueueReceive(s_button_queue, &gpio, 0) == pdTRUE) {
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "прерывание по спаду на GPIO%d", BUTTON_GPIO);

    gpio_config_t button = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        /* NEGEDGE — прерывание в момент перехода 1 -> 0, то есть на нажатии. */
        .intr_type = GPIO_INTR_NEGEDGE,
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

    /* Глубина 8: дребезг может дать несколько событий подряд, это нормально. */
    s_button_queue = xQueueCreate(8, sizeof(uint32_t));
    if (s_button_queue == NULL) {
        ESP_LOGE(TAG, "не удалось создать очередь");
        return;
    }

    /*
     * Сервис прерываний GPIO ставится один раз на всё приложение.
     * Аргумент — флаги. 0 означает «без особых требований к памяти».
     * Ноль в ESP_INTR_FLAG_IRAM понадобился бы, только если обработчик
     * обязан работать при выключенном кэше flash.
     */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr, (void *)BUTTON_GPIO));

    /*
     * xTaskCreate в ESP-IDF принимает размер стека в байтах, не в словах.
     * Это отличие от «классического» FreeRTOS, где стек задают в словах.
     * 3072 байта с запасом хватает на ESP_LOGI.
     * Приоритет 5 выше приоритета задачи main (обычно 1): реакция на
     * кнопку важнее фонового цикла, если бы он здесь был.
     */
    BaseType_t ok = xTaskCreate(button_task, "button", 3072, NULL, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "не удалось создать задачу кнопки");
    }
}
