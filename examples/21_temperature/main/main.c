/*
 * Пример 21. Внутренний датчик температуры кристалла.
 *
 * У ESP32-C3 внутри есть датчик температуры самого чипа, не воздуха
 * вокруг платы. Пока процессор и радио работают, кристалл теплее комнаты
 * на несколько градусов и больше. По этому числу нельзя судить о погоде.
 * Им можно заметить перегрев платы или просто убедиться, что датчик жив.
 *
 * Диапазон в конфигурации — подсказка драйверу, какие внутренние
 * коэффициенты выбрать. Измерение вне этого окна не обрезается молча:
 * драйвер может вернуть ошибку или значение худшей точности.
 * Окно 10…50 °C для работающей платы в комнате подходит.
 *
 * Внешний датчик (например на шине I2C) этим примером не читается.
 * Как найти такой датчик на шине, показано в примере 10.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"

static const char *TAG = "temp";

void app_main(void)
{
    temperature_sensor_handle_t sensor = NULL;
    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&config, &sensor));
    ESP_ERROR_CHECK(temperature_sensor_enable(sensor));
    ESP_LOGI(TAG, "датчик включён, окно %d…%d °C. Это температура кристалла",
             10, 50);

    while (true) {
        float celsius = 0.0f;
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(sensor, &celsius));
        ESP_LOGI(TAG, "%.2f °C", celsius);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
