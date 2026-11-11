/*
 * Пример 07. ШИМ на периферии LEDC: плавная яркость светодиода.
 *
 * ШИМ (PWM) быстро включает и выключает вывод. Глаз не видит мелькание,
 * если частота выше примерно 200 Гц, и воспринимает среднюю яркость.
 * Доля времени, которую вывод проводит в состоянии «включено», называется
 * скважностью, или duty. В драйвере LEDC это не процент, а число:
 * при разрешении 13 бит полный диапазон — от 0 до 8191.
 *
 * LEDC (LED Controller) — аппаратный генератор ШИМ в ESP32.
 * Он сам переключает вывод, процессор в это время свободен.
 * На ESP32-C3 есть только низкоскоростной режим LEDC_LOW_SPEED_MODE.
 * Для светодиода его с запасом хватает: здесь частота 5 кГц.
 *
 * Периферия умеет и саму плавную смену duty (fade): задаётся целевое
 * значение и время, а промежуточные шаги делает железо.
 */

/*
 * Библиотеки:
 * freertos/FreeRTOS.h и task.h — пауза между фазами «разгорается» и «гаснет».
 *   Саму яркость во время паузы меняет периферия, процессор только ждёт.
 * driver/gpio.h — номер вывода GPIO_NUM_8. Сам ШИМ вывод переключает LEDC,
 *   но тип gpio_num_t объявлен в этом заголовке.
 * driver/ledc.h — аппаратный генератор ШИМ: частота, скважность и плавное
 *   изменение скважности без участия цикла программы.
 * esp_log.h — сообщения о том, в какую сторону сейчас меняется яркость.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define LED_GPIO GPIO_NUM_8
/*
 * 0 — светодиод ярче, когда на выводе больше времени низкий уровень
 *     (типичная плата, где светодиод подтянут к 3.3 В).
 * 1 — светодиод ярче при большей доле высокого уровня.
 */
#define LED_ON_LEVEL 0

#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_BITS LEDC_TIMER_13_BIT
#define LEDC_FREQ_HZ 5000

static const char *TAG = "pwm";

/* Максимальное значение duty при выбранном разрешении: 2^13 - 1 = 8191. */
static uint32_t max_duty(void)
{
    return (1U << LEDC_DUTY_BITS) - 1U;
}

/*
 * Переводит яркость 0..100 в значение duty с учётом полярности светодиода.
 * Яркость 100 всегда означает «светит сильнее всего», даже если для этого
 * аппаратный duty должен быть нулём.
 */
static uint32_t duty_for_brightness(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint32_t full = max_duty();
    uint32_t duty = (full * (uint32_t)percent) / 100U;
    if (LED_ON_LEVEL == 0) {
        duty = full - duty;
    }
    return duty;
}

static void ledc_setup(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,          /* На C3 доступен только низкоскоростной режим. */
        .duty_resolution = LEDC_DUTY_BITS, /* 13 бит: яркость задаётся числом 0…8191. */
        .timer_num = LEDC_TIMER,          /* Какой из аппаратных таймеров LEDC брать. */
        .freq_hz = LEDC_FREQ_HZ,          /* Как часто вывод переключается, здесь 5 кГц. */
        .clk_cfg = LEDC_AUTO_CLK,         /* Источник такта драйвер подберёт сам. */
    };
    /* Таймер задаёт частоту. К выводу он пока не привязан. */
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = LED_GPIO,  /* Какой вывод переключать. */
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL, /* Канал хранит скважность и номер вывода. */
        .timer_sel = LEDC_TIMER, /* Частоту канал берёт у таймера, настроенного выше. */
        .duty = duty_for_brightness(0), /* Стартуем с погашенного светодиода. */
        .hpoint = 0, /* Момент внутри периода, когда начинается импульс. 0 — с начала. */
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    /*
     * fade_func_install ставит обработчик, который двигает duty.
     * Аргумент — флаги прерывания. 0 означает обычную установку.
     * Без этого вызова ledc_set_fade_with_time вернёт ошибку.
     */
    ESP_ERROR_CHECK(ledc_fade_func_install(0));
}

/* Плавно прийти к яркости percent за time_ms миллисекунд. */
static void fade_to(int percent, int time_ms)
{
    ESP_ERROR_CHECK(ledc_set_fade_with_time(
        LEDC_MODE, LEDC_CHANNEL, duty_for_brightness(percent), time_ms));
    /* LEDC_FADE_NO_WAIT: функция возвращается сразу, яркость меняет периферия. */
    ESP_ERROR_CHECK(ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_NO_WAIT));
}

void app_main(void)
{
    ESP_LOGI(TAG, "ШИМ %d Гц на GPIO%d, разрешение %d бит",
             LEDC_FREQ_HZ, LED_GPIO, LEDC_DUTY_BITS);
    ledc_setup();

    while (true) {
        ESP_LOGI(TAG, "яркость 0 -> 100");
        fade_to(100, 1500);
        vTaskDelay(pdMS_TO_TICKS(1600));

        ESP_LOGI(TAG, "яркость 100 -> 0");
        fade_to(0, 1500);
        vTaskDelay(pdMS_TO_TICKS(1600));
    }
}
