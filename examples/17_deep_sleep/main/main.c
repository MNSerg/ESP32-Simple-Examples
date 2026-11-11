/*
 * Пример 17. Глубокий сон.
 *
 * В глубоком сне процессор выключен, почти вся оперативная память
 * обесточена, программа не «продолжается со следующей строки».
 * Пробуждение выглядит как новая загрузка: снова выполняется app_main.
 * Ток в этом режиме — единицы микроампер, а не десятки миллиампер.
 * Точные цифры для вашей ревизии чипа смотрите в даташите, раздел
 * Power Consumption. Здесь важно отличие режимов, а не одна цифра.
 *
 * Что выживает:
 *   RTC-память, помеченная RTC_DATA_ATTR. Счётчик ниже растёт от
 *   пробуждения к пробуждению и обнуляется, только если снять питание.
 * Обычные глобальные переменные после глубокого сна мусорны: их
 * область снова инициализирует загрузчик.
 *
 * Чем разбудить ESP32-C3:
 *   таймер — всегда;
 *   GPIO — только выводы домена VDD3P3_RTC, это GPIO0…GPIO5.
 * Кнопка BOOT на GPIO9 глубокий сон не разбудит. Для кнопки в этом
 * примере нужен GPIO4 (или другой из 0…5), замкнутый на землю.
 * Таймер на 20 секунд сработает и без кнопки, так пример можно
 * проверить сразу.
 *
 * esp_sleep_get_wakeup_cause() имеет смысл только в самом начале
 * app_main: причина относится к пробуждению, которое только что
 * запустило эту загрузку.
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"

/* GPIO4 входит в RTC-домен. GPIO9, кнопка из проекта с дисплеем, — нет. */
#define WAKE_GPIO GPIO_NUM_4
#define SLEEP_SECONDS 20

static const char *TAG = "deep_sleep";

/* Значение лежит в RTC slow memory и сохраняется на время глубокого сна. */
RTC_DATA_ATTR static uint32_t s_wakeups;

static const char *cause_text(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
        return "таймер";
    case ESP_SLEEP_WAKEUP_GPIO:
        return "GPIO";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return "включение или сброс, это не пробуждение из сна";
    default:
        return "другой источник";
    }
}

void app_main(void)
{
    s_wakeups++;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "загрузка %" PRIu32 ", причина: %s (%d)",
             s_wakeups, cause_text(cause), (int)cause);

    /*
     * Таймер глубокого сна считает микросекунды.
     * 20 секунд * 1 000 000. Суффикс ULL не даёт переполнения 32-битного int.
     */
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL));

    /*
     * Низкий уровень на GPIO4 тоже будит чип.
     * Функция сама не настраивает подтяжку до входа в сон: при включённой
     * опции ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS (она включена по
     * умолчанию) нужная подтяжка ставится уже внутри esp_deep_sleep_start.
     * Для пробуждения низким уровнем внешняя подтяжка к 3.3 В надёжнее
     * внутренней. Кнопку между GPIO4 и GND как раз так и подключают.
     */
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(
        1ULL << WAKE_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW));

    ESP_LOGI(TAG, "ухожу в глубокий сон на %d с. Либо замкните GPIO%d на GND",
             SLEEP_SECONDS, WAKE_GPIO);
    /* Дать логу доехать до USB, иначе последнюю строку не увидите. */
    vTaskDelay(pdMS_TO_TICKS(200));

    /*
     * Отсюда управление не возвращается. Следующая выполненная строка
     * вашей программы — снова начало app_main после перезагрузки.
     */
    esp_deep_sleep_start();
}
