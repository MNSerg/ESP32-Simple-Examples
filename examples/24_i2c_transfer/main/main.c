/*
 * Пример 24. Запись и чтение I2C, и что делать с ошибкой.
 *
 * На ESP32-C3 один контроллер I2C, поэтому мастер и ведомый на одном
 * чипе одновременно не живут. Этот пример — мастер. Он умеет три
 * операции: записать байты, прочитать байты и записать адрес регистра
 * с повторным стартом, не отпуская шину между записью и чтением.
 *
 * Ошибки разные, и повторять их все подряд вредно.
 *   ESP_ERR_NOT_FOUND        — ответ i2c_master_probe, по адресу NACK.
 *   ESP_ERR_INVALID_RESPONSE — тот же NACK уже у transmit / receive.
 *   ESP_ERR_TIMEOUT          — шина не закончила обмен. Здесь помогает
 *                              один сброс i2c_master_bus_reset.
 *   ESP_ERR_INVALID_ARG      — ошибка в вызове. Повтор ничего не чинит.
 *
 * NACK у живой микросхемы иногда значит «занята», а не «её нет».
 * EEPROM после записи несколько миллисекунд не отвечает, и probe
 * в это время возвращает NOT_FOUND. Это нормальный цикл записи.
 *
 * Адрес 0x01 в примере пустой специально: ошибка видна даже без
 * подключённых микросхем. OLED 0x3C и EEPROM 0x50 используются,
 * только если probe получил ACK.
 */

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2c";

#define I2C_SDA GPIO_NUM_5
#define I2C_SCL GPIO_NUM_6
#define I2C_PORT I2C_NUM_0
#define I2C_FREQ_HZ 100000
#define XFER_TIMEOUT_MS 50

#define ADDR_EMPTY 0x01
#define ADDR_OLED 0x3C
#define ADDR_EEPROM 0x50
#define EEPROM_CELL 0x10
#define EEPROM_VALUE 0xA5

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_empty;
static i2c_master_dev_handle_t s_oled;
static i2c_master_dev_handle_t s_eeprom;
static bool s_eeprom_stored;

static const char *explain(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ACK, обмен прошёл";
    case ESP_ERR_NOT_FOUND:
        return "probe получил NACK, адрес молчит";
    case ESP_ERR_INVALID_RESPONSE:
        return "NACK во время записи или чтения";
    case ESP_ERR_TIMEOUT:
        return "таймаут, шина не освободилась";
    case ESP_ERR_INVALID_ARG:
        return "неверные аргументы, повтор бессмыслен";
    default:
        return esp_err_to_name(err);
    }
}

static void recover_if_stuck(esp_err_t err)
{
    if (err != ESP_ERR_TIMEOUT) {
        return;
    }
    esp_err_t reset = i2c_master_bus_reset(s_bus);
    if (reset == ESP_OK) {
        ESP_LOGW(TAG, "шина сброшена после таймаута");
    } else {
        ESP_LOGE(TAG, "сброс шины не удался: %s", esp_err_to_name(reset));
    }
}

/*
 * NACK у устройства, которое только что отвечало, пробуем ещё два раза:
 * микросхема могла быть занята. Таймаут сбрасывает шину один раз.
 * Неверный аргумент наружу отдаётся сразу.
 */
static esp_err_t transfer_retry(i2c_master_dev_handle_t dev, const uint8_t *write_data, size_t write_len,
                                uint8_t *read_data, size_t read_len)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (write_len > 0 && read_len > 0) {
            err = i2c_master_transmit_receive(dev, write_data, write_len, read_data, read_len, XFER_TIMEOUT_MS);
        } else if (write_len > 0) {
            err = i2c_master_transmit(dev, write_data, write_len, XFER_TIMEOUT_MS);
        } else {
            err = i2c_master_receive(dev, read_data, read_len, XFER_TIMEOUT_MS);
        }

        if (err == ESP_OK || err == ESP_ERR_INVALID_ARG) {
            return err;
        }
        if (err == ESP_ERR_TIMEOUT) {
            recover_if_stuck(err);
            return err;
        }
        if (attempt < 3) {
            ESP_LOGW(TAG, "попытка %d: %s", attempt, explain(err));
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    return err;
}

static bool add_device(uint16_t address, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &cfg, out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "не удалось зарегистрировать 0x%02X: %s", address, explain(err));
        *out = NULL;
        return false;
    }
    return true;
}

static bool probe_ok(uint16_t address)
{
    esp_err_t err = i2c_master_probe(s_bus, address, XFER_TIMEOUT_MS);
    if (err == ESP_OK) {
        return true;
    }
    recover_if_stuck(err);
    ESP_LOGI(TAG, "probe 0x%02X: %s", address, explain(err));
    return false;
}

static void demo_missing(void)
{
    esp_err_t probe = i2c_master_probe(s_bus, ADDR_EMPTY, XFER_TIMEOUT_MS);
    ESP_LOGI(TAG, "пустой адрес 0x%02X, probe: %s", ADDR_EMPTY, explain(probe));
    recover_if_stuck(probe);
    if (probe == ESP_OK || s_empty == NULL) {
        return;
    }

    uint8_t byte = 0x00;
    esp_err_t err = i2c_master_transmit(s_empty, &byte, 1, XFER_TIMEOUT_MS);
    ESP_LOGI(TAG, "запись в 0x%02X без повторов: %s", ADDR_EMPTY, explain(err));
    recover_if_stuck(err);

    uint8_t read_back = 0;
    err = i2c_master_receive(s_empty, &read_back, 1, XFER_TIMEOUT_MS);
    ESP_LOGI(TAG, "чтение из 0x%02X: %s", ADDR_EMPTY, explain(err));
    recover_if_stuck(err);
}

static void demo_oled(void)
{
    if (!probe_ok(ADDR_OLED) || s_oled == NULL) {
        ESP_LOGI(TAG, "OLED 0x%02X нет, запись и чтение дисплея пропущены", ADDR_OLED);
        return;
    }

    /*
     * 0x00 — «дальше команда контроллера», 0xAF — включить матрицу.
     * Повтор этой команды экран не портит.
     */
    uint8_t command[] = {0x00, 0xAF};
    esp_err_t err = transfer_retry(s_oled, command, sizeof(command), NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "команда дисплею не ушла: %s", explain(err));
        return;
    }
    ESP_LOGI(TAG, "в OLED записано включение экрана");

    /*
     * Один байт чтения с 0x3C — регистр состояния SSD1306.
     * Часть модулей на чтение не отвечает, хотя запись у них работает.
     */
    uint8_t status = 0;
    err = transfer_retry(s_oled, NULL, 0, &status, 1);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "статус OLED: 0x%02X", status);
    } else {
        ESP_LOGW(TAG, "этот модуль не отдал статус: %s", explain(err));
    }
}

static void eeprom_wait_ready(void)
{
    for (int attempt = 1; attempt <= 20; attempt++) {
        esp_err_t err = i2c_master_probe(s_bus, ADDR_EEPROM, XFER_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "EEPROM снова отвечает, попытка %d", attempt);
            return;
        }
        if (err == ESP_ERR_TIMEOUT) {
            recover_if_stuck(err);
            return;
        }
        /* NOT_FOUND здесь значит «идёт цикл записи», а не «микросхемы нет». */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(TAG, "EEPROM не ответила после записи");
}

static void demo_eeprom(void)
{
    if (!probe_ok(ADDR_EEPROM) || s_eeprom == NULL) {
        ESP_LOGI(TAG, "EEPROM 0x%02X нет, проверка ячейки пропущена", ADDR_EEPROM);
        return;
    }

    if (!s_eeprom_stored) {
        uint8_t payload[] = {EEPROM_CELL, EEPROM_VALUE};
        esp_err_t err = transfer_retry(s_eeprom, payload, sizeof(payload), NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "запись EEPROM не удалась: %s", explain(err));
            return;
        }
        ESP_LOGI(TAG, "в ячейку 0x%02X записан 0x%02X, ждём конец цикла записи", EEPROM_CELL, EEPROM_VALUE);
        eeprom_wait_ready();
        s_eeprom_stored = true;
    }

    uint8_t cell = EEPROM_CELL;
    uint8_t value = 0;
    /*
     * transmit_receive держит шину: адрес ячейки и чтение идут
     * с повторным стартом. Две отдельные транзакции здесь неверны,
     * указатель внутри EEPROM между ними никто не обещает сохранить.
     */
    esp_err_t err = transfer_retry(s_eeprom, &cell, 1, &value, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "чтение EEPROM не удалось: %s", explain(err));
        return;
    }
    if (value != EEPROM_VALUE) {
        ESP_LOGE(TAG, "шина исправна, но данные не сошлись: ждали 0x%02X, прочитали 0x%02X",
                 EEPROM_VALUE, value);
        return;
    }
    ESP_LOGI(TAG, "EEPROM вернула 0x%02X, запись и чтение совпали", value);
}

void app_main(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "шина не открылась: %s. Проверьте GPIO5, GPIO6 и подтяжки к 3.3 В",
                 esp_err_to_name(err));
        return;
    }

    add_device(ADDR_EMPTY, &s_empty);
    add_device(ADDR_OLED, &s_oled);
    add_device(ADDR_EEPROM, &s_eeprom);
    ESP_LOGI(TAG, "SDA=GPIO%d SCL=GPIO%d. Ошибки ниже для пустого адреса ожидаемы", I2C_SDA, I2C_SCL);

    while (1) {
        demo_missing();
        demo_oled();
        demo_eeprom();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
