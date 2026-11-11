/*
 * Пример 06. Два точных таймера: esp_timer и аппаратный GPTimer.
 *
 * esp_timer
 *   Системный таймер ESP-IDF с разрешением 1 микросекунда.
 *   Колбэк по умолчанию вызывается из задачи esp_timer, не из прерывания,
 *   поэтому в нём можно писать в лог. Им пользуются драйверы Wi-Fi и таймауты.
 *   Для своего кода это основной «точный таймер», пока не нужна отдельная
 *   аппаратная периферия.
 *
 * GPTimer
 *   Отдельный аппаратный счётчик (general purpose timer).
 *   По совпадению с числом сравнения он вызывает функцию уже из прерывания.
 *   В колбэке действуют те же запреты, что в прерывании кнопки:
 *   только короткие FromISR-операции. Здесь прерывание кладёт единицу
 *   в очередь, а печатает обычная задача.
 *
 * Программный таймер FreeRTOS из примера 05 грубее: его шаг — тик ОС,
 * обычно 10 мс. Эти два таймера нужны, когда 10 мс уже много.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gptimer.h"
#include "esp_log.h"

static const char *TAG = "hw_timer";

static QueueHandle_t s_gptimer_queue;
static int s_esp_timer_ticks;

/* Колбэк esp_timer выполняется в задаче, не в прерывании. Лог здесь разрешён. */
static void esp_timer_callback(void *arg)
{
    (void)arg;
    s_esp_timer_ticks++;
    int64_t now_us = esp_timer_get_time();
    ESP_LOGI(TAG, "esp_timer тик %d, время от старта чипа %lld мкс",
             s_esp_timer_ticks, (long long)now_us);
}

/*
 * Колбэк GPTimer выполняется в прерывании.
 * Возврат true просит планировщик сразу переключиться на задачу,
 * которую разбудил xQueueSendFromISR. false — переключение можно отложить.
 */
static bool IRAM_ATTR gptimer_alarm(gptimer_handle_t timer,
                                    const gptimer_alarm_event_data_t *event,
                                    void *user_ctx)
{
    (void)timer;
    (void)event;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    uint32_t mark = 1;
    BaseType_t wake_higher_priority = pdFALSE;
    xQueueSendFromISR(queue, &mark, &wake_higher_priority);
    return wake_higher_priority == pdTRUE;
}

static void gptimer_print_task(void *arg)
{
    (void)arg;
    uint32_t mark = 0;
    int count = 0;
    while (true) {
        if (xQueueReceive(s_gptimer_queue, &mark, portMAX_DELAY) == pdTRUE) {
            count++;
            ESP_LOGI(TAG, "GPTimer, срабатывание %d", count);
        }
    }
}

static void start_esp_timer(void)
{
    const esp_timer_create_args_t args = {
        .callback = esp_timer_callback,
        .arg = NULL,
        /* ESP_TIMER_TASK — колбэк в задаче. ESP_TIMER_ISR — колбэк прямо из прерывания. */
        .dispatch_method = ESP_TIMER_TASK,
        .name = "second",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    /* Период задаётся в микросекундах. 1000000 мкс = 1 с. */
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000));
}

static void start_gptimer(void)
{
    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        /* 1 МГц: одна единица счётчика равна 1 микросекунде. */
        .resolution_hz = 1000000,
    };
    gptimer_handle_t timer = NULL;
    ESP_ERROR_CHECK(gptimer_new_timer(&config, &timer));

    gptimer_alarm_config_t alarm = {
        .reload_count = 0,
        /* Срабатывание каждые 200000 мкс = 200 мс. */
        .alarm_count = 200000,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));

    gptimer_event_callbacks_t callbacks = {
        .on_alarm = gptimer_alarm,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, s_gptimer_queue));

    /*
     * Порядок фиксированный: сначала enable (разрешает прерывание и
     * захватывает ресурсы), потом start (счётчик побежал).
     */
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_ERROR_CHECK(gptimer_start(timer));
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp_timer раз в 1 с, GPTimer раз в 200 мс");

    s_gptimer_queue = xQueueCreate(16, sizeof(uint32_t));
    if (s_gptimer_queue == NULL) {
        ESP_LOGE(TAG, "нет памяти под очередь GPTimer");
        return;
    }

    xTaskCreate(gptimer_print_task, "gptimer", 3072, NULL, 5, NULL);
    start_esp_timer();
    start_gptimer();
}
