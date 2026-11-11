/*
 * Пример 04. Несколько задач FreeRTOS: очередь и мьютекс.
 *
 * ESP-IDF не выполняет вашу программу «в один поток от начала до конца».
 * Планировщик FreeRTOS делит время между задачами. На ESP32-C3 ядро одно,
 * поэтому в каждый момент выполняется только одна задача, но переключение
 * происходит так часто, что задачи выглядят одновременными.
 *
 * В этом примере три задачи:
 *   producer  — раз в секунду кладёт в очередь «измерение»;
 *   consumer  — ждёт измерение и печатает его;
 *   counter   — раз в 200 мс увеличивает общий счётчик.
 *
 * Очередь передаёт данные от одной задачи к другой и при этом
 * будит получателя. Общий счётчик так передавать нельзя: его
 * читают и пишут из разных задач, поэтому доступ закрыт мьютексом.
 */

#include <inttypes.h> /* PRIu32 для печати миллисекунд, которые лежат в uint32_t. */

/*
 * Библиотеки:
 * freertos/FreeRTOS.h — типы планировщика, подключается до queue.h, semphr.h и task.h.
 * freertos/queue.h — очередь измерений: producer кладёт, consumer забирает.
 * freertos/semphr.h — мьютекс. Им закрывают общий счётчик от одновременного доступа.
 * freertos/task.h — xTaskCreate запускает три задачи, vTaskDelay задаёт их ритм.
 * esp_log.h — печать из задач. У каждой строки будет подпись «tasks».
 */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "tasks";

/* Одно «измерение», которое producer передаёт consumer. Очередь копирует всю структуру. */
typedef struct {
    int value;         /* Порядковый номер измерения, его увеличивает producer. */
    uint32_t time_ms;  /* Сколько миллисекунд прошло с запуска планировщика. */
} sample_t;

static QueueHandle_t s_samples;
static SemaphoreHandle_t s_counter_lock;
static int s_shared_counter;

static void producer_task(void *arg)
{
    (void)arg;
    int value = 0;
    while (true) {
        sample_t sample = {
            .value = value,
            /*
             * xTaskGetTickCount — сколько тиков прошло с запуска FreeRTOS.
             * portTICK_PERIOD_MS — длина одного тика в миллисекундах, обычно 10.
             * Произведение — время в миллисекундах на момент отправки.
             */
            .time_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
        };
        /*
         * portMAX_DELAY: если очередь заполнена, producer заснёт,
         * пока consumer не заберёт элемент. Третий аргумент — как долго
         * ждать свободного места. Данные копируются в очередь побайтно,
         * поэтому локальная переменная sample после отправки уже не нужна.
         */
        if (xQueueSend(s_samples, &sample, portMAX_DELAY) != pdTRUE) {
            ESP_LOGW(TAG, "очередь не приняла измерение");
        }
        value++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void consumer_task(void *arg)
{
    (void)arg;
    sample_t sample;
    while (true) {
        if (xQueueReceive(s_samples, &sample, portMAX_DELAY) == pdTRUE) {
            /* Читаем общий счётчик только под мьютексом. */
            int counter_copy = 0;
            if (xSemaphoreTake(s_counter_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                counter_copy = s_shared_counter;
                xSemaphoreGive(s_counter_lock);
            }
            ESP_LOGI(TAG, "измерение %d в момент %" PRIu32 " мс, общий счётчик %d",
                     sample.value, sample.time_ms, counter_copy);
        }
    }
}

static void counter_task(void *arg)
{
    (void)arg;
    while (true) {
        /*
         * Мьютекс нужен даже для «простого» s_shared_counter++.
         * На одном ядре инкремент всё равно может быть перебит посередине
         * последовательности чтение-изменение-запись, если компилятор
         * разложил её на несколько команд. Пока мьютекс занят, другая
         * задача, которая его ждёт, не войдёт в этот же участок.
         */
        if (xSemaphoreTake(s_counter_lock, portMAX_DELAY) == pdTRUE) {
            s_shared_counter++;
            xSemaphoreGive(s_counter_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "три задачи: producer, consumer, counter");

    /* Очередь на 4 измерения. Пятое заставит producer подождать. */
    s_samples = xQueueCreate(4, sizeof(sample_t));
    /* Мьютекс создаётся свободным. Первый Take проходит сразу. */
    s_counter_lock = xSemaphoreCreateMutex();
    if (s_samples == NULL || s_counter_lock == NULL) {
        ESP_LOGE(TAG, "не хватило памяти на очередь или мьютекс");
        return;
    }

    /*
     * Приоритет: чем больше число, тем важнее задача.
     * У всех трёх приоритет одинаковый (2). Они не крутятся без пауз,
     * а спят на очереди и на vTaskDelay, поэтому ни одна не «займёт ядро навсегда».
     * Стек — в байтах.
     */
    xTaskCreate(producer_task, "producer", 3072, NULL, 2, NULL);
    xTaskCreate(consumer_task, "consumer", 3072, NULL, 2, NULL);
    xTaskCreate(counter_task, "counter", 2048, NULL, 2, NULL);

    /*
     * app_main — тоже задача. Когда она возвращается, FreeRTOS её удаляет.
     * Остальные задачи продолжают работать. Здесь возвращаться рано:
     * пусть main просто спит и не мешает.
     */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "задача main жива, в очереди %u элементов",
                 (unsigned)uxQueueMessagesWaiting(s_samples));
    }
}
