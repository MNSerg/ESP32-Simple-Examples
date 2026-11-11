/*
 * Пример 08. АЦП: чтение напряжения на GPIO4.
 *
 * АЦП (ADC) превращает напряжение на входе в целое число.
 * На ESP32-C3 разрядность по умолчанию — 12 бит: сырое значение лежит
 * в диапазоне 0…4095. Само по себе это число ещё не вольты. Перевод
 * в милливольты делает калибровка, записанная в чип на заводе
 * (схема curve fitting).
 *
 * У АЦП есть ослабление (attenuation). Оно выбирает, какой диапазон
 * напряжений растянуть на эти 4095 отсчётов. ADC_ATTEN_DB_12 на C3
 * покрывает примерно 0…3.3 В — как раз питание платы.
 * Без ослабления верхняя граница была бы около 1 В, и 3.3 В уже
 * нечем было бы отличить от перегрузки.
 *
 * В ESP-IDF 6 старый драйвер driver/adc.h удалён. Нужен oneshot-драйвер
 * из esp_adc/adc_oneshot.h: одно измерение по явному вызову.
 *
 * Важно: на ESP32-C3 блок ADC2 делит ресурсы с Wi-Fi. Пока Wi-Fi выключен,
 * ADC2 ещё можно читать, но в обычной программе с сетью используют ADC1.
 * Этот пример читает только ADC1.
 */

/*
 * Библиотеки:
 * freertos/FreeRTOS.h и task.h — пауза полсекунды между измерениями.
 * esp_adc/adc_oneshot.h — одно измерение по вызову, без непрерывного потока.
 *   Старый driver/adc.h в ESP-IDF 6 удалён, поэтому заголовок именно такой.
 * esp_adc/adc_cali.h — перевод сырого кода АЦП в милливольты.
 * esp_adc/adc_cali_scheme.h — заводская калибровка curve fitting, она есть на C3.
 * esp_log.h — печать сырого кода и напряжения.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

/*
 * Канал ADC1 и вывод ESP32-C3:
 *   GPIO0 → ADC1_CHANNEL_0
 *   GPIO1 → ADC1_CHANNEL_1
 *   GPIO2 → ADC1_CHANNEL_2
 *   GPIO3 → ADC1_CHANNEL_3
 *   GPIO4 → ADC1_CHANNEL_4
 * GPIO0 и GPIO2 лучше не занимать без нужды: это strapping-выводы,
 * их уровень в момент сброса выбирает режим загрузки. GPIO4 спокойнее.
 */
#define ADC_GPIO_NAME "GPIO4"
#define ADC_CHANNEL ADC_CHANNEL_4
#define ADC_ATTEN ADC_ATTEN_DB_12
#define AVERAGE_SAMPLES 16

static const char *TAG = "adc";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

static void adc_setup(void)
{
    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = ADC_UNIT_1, /* ADC1 не конфликтует с Wi-Fi. ADC2 на C3 с радио делит ресурсы. */
        .ulp_mode = ADC_ULP_MODE_DISABLE, /* Сопроцессор ULP в этом примере не измеряет. */
    };
    /* Ручка блока АЦП. Дальше все чтения идут через s_adc, не через номер блока. */
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));

    adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN, /* Ослабление выбирает диапазон. DB_12 покрывает 0…3.3 В. */
        .bitwidth = ADC_BITWIDTH_DEFAULT, /* На C3 это 12 бит, сырое значение 0…4095. */
    };
    /* Привязать канал GPIO4 к уже созданному блоку. Пока канал не настроен, читать нечего. */
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL, &channel));

    /*
     * Калибровка может отсутствовать на совсем необычном чипе.
     * Тогда остаётся сырое значение, а милливольты пример не печатает.
     * На серийном ESP32-C3 схема curve fitting есть.
     */
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cal, &s_cali);
    if (err != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "калибровка недоступна (%s). Будут только сырые отсчёты",
                 esp_err_to_name(err));
    }
}

/* Среднее из нескольких чтений. Одиночный отсчёт АЦП заметно шумит. */
static int read_average_raw(void)
{
    int sum = 0;
    for (int i = 0; i < AVERAGE_SAMPLES; i++) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, ADC_CHANNEL, &raw));
        sum += raw;
    }
    return sum / AVERAGE_SAMPLES;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ADC1 канал %s, ослабление 12 дБ, усреднение %d",
             ADC_GPIO_NAME, AVERAGE_SAMPLES);
    ESP_LOGI(TAG, "подайте 0…3.3 В. Напряжение выше 3.3 В повредит вывод");
    adc_setup();

    while (true) {
        int raw = read_average_raw();
        if (s_cali != NULL) {
            int mv = 0;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_cali, raw, &mv));
            ESP_LOGI(TAG, "raw %4d,  %d мВ", raw, mv);
        } else {
            ESP_LOGI(TAG, "raw %4d", raw);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
