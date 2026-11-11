/*
 * Пример 10. Шина I2C: найти все отвечающие устройства.
 *
 * I2C — двухпроводная шина.
 *   SDA — данные,
 *   SCL — такт, его выдаёт мастер.
 * Несколько микросхем висят на одной паре проводов. У каждой свой
 * 7-битный адрес. Мастер в начале посылки называет адрес, отвечает
 * только тот, кого назвали. Если микросхема адрес узнала, она
 * прижимает SDA к нулю в специальном такте — это подтверждение, ACK.
 * Если по адресу никого нет, линия остаётся в единице — NACK.
 *
 * Сканер перебирает адреса от 0x08 до 0x77 и смотрит, где приходит ACK.
 * Адреса ниже 0x08 и выше 0x77 зарезервированы спецификацией.
 *
 * Выводы совпадают с исходным проектом дисплея: SDA = GPIO5, SCL = GPIO6.
 * OLED 72x40 из того проекта почти наверняка ответит по адресу 0x3C
 * (реже 0x3D). Сканер дисплей не перепрограммирует, только опрашивает адрес.
 *
 * Подтягивающие резисторы к 3.3 В на SDA и SCL обязательны. На модуле
 * OLED они обычно уже стоят. Внутренние подтяжки чипа (~45 кОм) для
 * коротких проводов тоже включают — они слабее внешних, но как добавка годны.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define I2C_SDA GPIO_NUM_5
#define I2C_SCL GPIO_NUM_6
#define I2C_PORT I2C_NUM_0
/* 100 кГц — спокойная скорость. OLED из исходного проекта работает и на 400 кГц. */
#define I2C_FREQ_HZ 100000

static const char *TAG = "i2c";

static i2c_master_bus_handle_t s_bus;

static void i2c_setup(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_bus));
}

/*
 * Показать, как отправить байты уже найденному устройству.
 * Для SSD1306 байт 0x00 означает «дальше команда», 0xAF — включить экран.
 * Если по адресу 0x3C сидит именно такой дисплей, он включится.
 * Другую микросхему с тем же адресом эти два байта могут сбить с толку,
 * поэтому запись выполняется только при I2C_DEMO_WRITE == 1.
 */
#define I2C_DEMO_WRITE 0

static void demo_write_ssd1306(uint8_t address)
{
#if I2C_DEMO_WRITE
    i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &device, &dev));

    uint8_t payload[] = {0x00, 0xAF};
    esp_err_t err = i2c_master_transmit(dev, payload, sizeof(payload), 100);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "в 0x%02X записана команда включения дисплея", address);
    } else {
        ESP_LOGW(TAG, "запись в 0x%02X не удалась: %s", address, esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev));
#else
    (void)address;
#endif
}

static void scan_once(void)
{
    ESP_LOGI(TAG, "сканирование SDA=GPIO%d SCL=GPIO%d", I2C_SDA, I2C_SCL);
    int found = 0;

    for (uint8_t address = 0x08; address <= 0x77; address++) {
        /*
         * i2c_master_probe не требует заранее регистрировать устройство.
         * Таймаут — в миллисекундах. Отсутствие микросхемы — ожидаемый
         * результат почти для всех адресов, это не повод останавливать программу
         * через ESP_ERROR_CHECK.
         */
        esp_err_t err = i2c_master_probe(s_bus, address, 20);
        if (err == ESP_OK) {
            found++;
            ESP_LOGI(TAG, "  устройство отвечает по адресу 0x%02X", address);
            if (address == 0x3C || address == 0x3D) {
                ESP_LOGI(TAG, "  это типичный адрес OLED SSD1306, как в проекте с дисплеем");
                demo_write_ssd1306(address);
            }
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "никто не ответил. Проверьте провода SDA, SCL, питание и землю");
    } else {
        ESP_LOGI(TAG, "найдено устройств: %d", found);
    }
}

void app_main(void)
{
    i2c_setup();
    while (true) {
        scan_once();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
