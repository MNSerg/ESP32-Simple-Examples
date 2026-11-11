/*
 * Характеристики платы на OLED 72x40.
 *
 * Четыре страницы не помещаются сразу, поэтому экран сам листает
 * их раз в три секунды. Короткое или длинное нажатие GPIO9
 * переключает страницу сразу и откладывает автолистание.
 *
 * Температура здесь — датчик внутри кристалла, не воздух в комнате.
 * Пока чип только что включился, она близка к температуре платы.
 */

#include <stdio.h>    /* snprintf собирает строки для экрана. */
#include <inttypes.h> /* PRIu32 и PRId64 печатают кучу и секунды без обрезки типа. */

/*
 * Библиотеки:
 * driver/gpio.h — кнопка GPIO9, она листает страницы.
 * driver/temperature_sensor.h — датчик внутри кристалла, не температура воздуха.
 * esp_idf_version.h — esp_get_idf_version возвращает строку версии ESP-IDF.
 * esp_log.h — те же строки, что на стекле, дублируются в монитор.
 * esp_system.h — свободная куча и esp_reset_reason, причина последней перезагрузки.
 * esp_timer.h — микросекунды с запуска. Делим на 1 000 000 и получаем секунды работы.
 * freertos/FreeRTOS.h и task.h — антидребезг кнопки и пауза между проверками.
 * u8g2.h и u8g2_esp32_hal.h — буфер кадра OLED и привязка этого буфера к I2C.
 */
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

static const char *TAG = "status";

#define OLED_ADDR 0x3C
#define BUTTON_GPIO GPIO_NUM_9
#define PAGE_COUNT 4
#define AUTO_FLIP_US 3000000
#define REDRAW_US 1000000

static u8g2_t s_u8g2;
static temperature_sensor_handle_t s_temp;

static const char *reset_text(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "питание";
    case ESP_RST_SW:
        return "программа";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "просадка";
    case ESP_RST_USB:
        return "USB";
    default:
        return "другое";
    }
}

static bool display_init(void)
{
    u8g2_hal_config_t config = U8G2_HAL_CONFIG_DEFAULT;
    config.i2c.sda = GPIO_NUM_5;
    config.i2c.scl = GPIO_NUM_6;
    config.i2c.port = I2C_NUM_0;
    config.i2c.clk_speed = 400000;

    if (u8g2_hal_init(&config) != ESP_OK) {
        return false;
    }

    /* Профиль SSD1306 72×40. _f — полный буфер: рисуем в памяти, потом одним разом на стекло. */
    u8g2_Setup_ssd1306_i2c_72x40_er_f(
        &s_u8g2,
        U8G2_R0,
        u8g2_esp32_hal_i2c_byte_cb,
        u8g2_esp32_hal_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&s_u8g2.u8x8, OLED_ADDR << 1);
    u8g2_InitDisplay(&s_u8g2);      /* Последовательность включения контроллера SSD1306. */
    u8g2_SetPowerSave(&s_u8g2, 0);  /* 0 выводит стекло из сна. */
    u8g2_SetFont(&s_u8g2, u8g2_font_5x8_t_cyrillic); /* Кириллица есть, но её рисует только DrawUTF8. */
    u8g2_SetContrast(&s_u8g2, 128); /* Середина диапазона 0…255, чтобы текст было видно. */
    return true;
}

static bool temp_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&cfg, &s_temp) != ESP_OK) {
        s_temp = NULL;
        return false;
    }
    if (temperature_sensor_enable(s_temp) != ESP_OK) {
        temperature_sensor_uninstall(s_temp);
        s_temp = NULL;
        return false;
    }
    return true;
}

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static bool button_pressed(void)
{
    if (gpio_get_level(BUTTON_GPIO) != 0) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
    if (gpio_get_level(BUTTON_GPIO) != 0) {
        return false;
    }
    while (gpio_get_level(BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(30));
    return true;
}

static void draw_page(int page)
{
    char line2[24];
    char line3[24];
    const char *title = "";

    switch (page) {
    case 0:
        title = "Чип";
        snprintf(line2, sizeof(line2), "ESP32-C3");
        /* Частота берётся из sdkconfig, это та частота, с которой собран проект. */
        snprintf(line3, sizeof(line3), "%d МГц", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
        break;
    case 1:
        title = "Память";
        snprintf(line2, sizeof(line2), "%" PRIu32 " б", esp_get_free_heap_size());
        snprintf(line3, sizeof(line3), "мин %" PRIu32, esp_get_minimum_free_heap_size());
        break;
    case 2: {
        /* esp_timer_get_time — микросекунды. Целое деление даёт прошедшие секунды. */
        int64_t seconds = esp_timer_get_time() / 1000000;
        title = "Время";
        snprintf(line2, sizeof(line2), "%" PRId64 " с", seconds);
        snprintf(line3, sizeof(line3), "%s", reset_text(esp_reset_reason()));
        break;
    }
    default: {
        float celsius = 0;
        title = "Кристалл";
        if (s_temp != NULL && temperature_sensor_get_celsius(s_temp, &celsius) == ESP_OK) {
            snprintf(line2, sizeof(line2), "%.1f C", celsius);
        } else {
            snprintf(line2, sizeof(line2), "--");
        }
        /* %.8s берёт начало строки версии: на узком экране длинный суффикс не влезает. */
        snprintf(line3, sizeof(line3), "IDF %.8s", esp_get_idf_version());
        break;
    }
    }

    u8g2_ClearBuffer(&s_u8g2); /* Стереть прошлую страницу в буфере, не на стекле. */
    u8g2_DrawUTF8(&s_u8g2, 0, 10, title);
    u8g2_DrawUTF8(&s_u8g2, 0, 22, line2);
    u8g2_DrawUTF8(&s_u8g2, 0, 34, line3);
    /* Точки внизу показывают, какая из четырёх страниц открыта. */
    for (int i = 0; i < PAGE_COUNT; i++) {
        int x = 28 + i * 6;
        if (i == page) {
            u8g2_DrawBox(&s_u8g2, x, 37, 3, 3);
        } else {
            u8g2_DrawPixel(&s_u8g2, x + 1, 38);
        }
    }
    u8g2_SendBuffer(&s_u8g2);
    ESP_LOGI(TAG, "%s | %s | %s", title, line2, line3);
}

void app_main(void)
{
    button_init();
    if (!temp_init()) {
        ESP_LOGW(TAG, "датчик температуры не открылся, на странице будет --");
        s_temp = NULL;
    }
    if (!display_init()) {
        ESP_LOGE(TAG, "дисплей не открылся, проверьте SDA=5 SCL=6 и адрес 0x3C");
        return;
    }

    int page = 0;
    int64_t last_draw = 0;
    int64_t last_flip = esp_timer_get_time();

    while (1) {
        bool flip = false;
        if (button_pressed()) {
            page = (page + 1) % PAGE_COUNT;
            last_flip = esp_timer_get_time();
            flip = true;
        } else if (esp_timer_get_time() - last_flip >= AUTO_FLIP_US) {
            page = (page + 1) % PAGE_COUNT;
            last_flip = esp_timer_get_time();
            flip = true;
        }

        int64_t now = esp_timer_get_time();
        if (flip || now - last_draw >= REDRAW_US) {
            draw_page(page);
            last_draw = now;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
