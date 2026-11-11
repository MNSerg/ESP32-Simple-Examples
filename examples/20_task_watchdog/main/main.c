/*
 * Пример 20. Сторожевой таймер задач.
 *
 * Сторож (watchdog) перезагружает чип, если программа перестала подавать
 * признаки жизни. Иначе зависший цикл навсегда оставил бы устройство
 * без связи и без реакции на кнопку.
 *
 * У ESP32 таких сторожей несколько, путать их не стоит:
 *   сторож задач (task WDT) — следит за задачами FreeRTOS, которые
 *     на него подписались. Если подписанная задача долго не вызывает
 *     esp_task_wdt_reset, чип перезагружается;
 *   сторож прерываний (interrupt WDT) — ловит ситуацию, когда прерывание
 *     не завершается или планировщик долго не получает управления;
 *   RTC-сторож — последний рубеж на время глубокого сна и загрузки.
 *
 * Этот пример показывает именно сторож задач.
 *
 * Сначала задача main пять раз кормит сторож и печатает, что всё хорошо.
 * Потом она перестаёт это делать и спит дольше таймаута. Чип перезагружается.
 * На следующем старте esp_reset_reason() сообщает, что виноват сторож задач.
 *
 * Кормить сторож из задачи, которая заведомо зависла, бессмысленно:
 * esp_task_wdt_reset ставят в конец нормального рабочего цикла,
 * а не в отдельный таймер «на всякий случай». Отдельный таймер
 * продолжит кормить сторож даже когда основной цикл уже мёртв.
 */

/*
 * Библиотеки:
 * freertos/FreeRTOS.h и task.h — пауза в одну секунду. Пока задача спит,
 *   она не кормит сторож, и именно это в конце примера его и срабатывает.
 * esp_log.h — журнал.
 * esp_system.h — esp_reset_reason говорит, почему чип загрузился в этот раз.
 * esp_task_wdt.h — подписать задачу на сторож и сбросить его таймер.
 *   Таймаут 3 секунды задан не здесь, а в sdkconfig.defaults.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

static const char *TAG = "wdt";

static const char *reset_reason_text(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "включение питания";
    case ESP_RST_EXT:
        return "внешний вывод сброса";
    case ESP_RST_SW:
        return "программный сброс";
    case ESP_RST_PANIC:
        return "паника: исключение или проверка ESP_ERROR_CHECK";
    case ESP_RST_INT_WDT:
        return "сторож прерываний";
    case ESP_RST_TASK_WDT:
        return "сторож задач";
    case ESP_RST_WDT:
        return "другой аппаратный сторож";
    case ESP_RST_DEEPSLEEP:
        return "выход из глубокого сна";
    case ESP_RST_BROWNOUT:
        return "просадка питания";
    case ESP_RST_USB:
        return "USB";
    default:
        return "неизвестная причина";
    }
}

void app_main(void)
{
    /* Читается один раз в начале: причина относится к загрузке, которая уже произошла. */
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "причина этой загрузки: %s (%d)", reset_reason_text(reason), (int)reason);

    /*
     * NULL означает «подписать текущую задачу», то есть main.
     * Пока задача подписана, она обязана регулярно вызывать esp_task_wdt_reset.
     * Таймаут задан в sdkconfig.defaults: 3 секунды.
     */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    for (int i = 1; i <= 5; i++) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        ESP_LOGI(TAG, "цикл %d, сторож покормлен", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGW(TAG, "больше не кормлю сторож. Через ~3 с будет перезагрузка");
    while (true) {
        /* Намеренно длинная пауза без esp_task_wdt_reset. */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
