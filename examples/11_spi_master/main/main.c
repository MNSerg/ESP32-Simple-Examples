/*
 * Пример 11. SPI-мастер: отправить байты и одновременно принять ответ.
 *
 * SPI — синхронная шина с отдельными проводами:
 *   SCLK — такт, его всегда выдаёт мастер (здесь ESP32-C3);
 *   MOSI — данные от мастера к устройству (Master Out Slave In);
 *   MISO — данные от устройства к мастеру (Master In Slave Out);
 *   CS   — выбор устройства, обычно активный уровень 0.
 *
 * В отличие от UART, тактовый провод общий, поэтому скорость можно
 * менять без «договорённости baud». В отличие от I2C, данные наружу
 * и внутрь идут по разным проводам и могут передаваться в один и тот же
 * момент: пока мастер выдвигает бит на MOSI, устройство выдвигает бит на MISO.
 *
 * Проверка без внешней микросхемы — петля: перемычка между MOSI и MISO.
 * Тогда принятый буфер совпадёт с отправленным.
 *
 * SPI0 и SPI1 на ESP32 заняты внешней flash, из которой выполняется код.
 * Для своих устройств берут SPI2.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#define PIN_SCLK GPIO_NUM_6
#define PIN_MOSI GPIO_NUM_7
#define PIN_MISO GPIO_NUM_2
#define PIN_CS GPIO_NUM_10
#define CLOCK_HZ 1000000

static const char *TAG = "spi";

static spi_device_handle_t s_device;

static void spi_setup(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        /* Линии quad-SPI нужны только флешкам. Обычному устройству их нет. */
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    /*
     * SPI_DMA_DISABLED: четыре байта процессор перенесёт сам.
     * DMA (SPI_DMA_CH_AUTO) имеет смысл для длинных буферов, например кадра дисплея.
     */
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t device = {
        .clock_speed_hz = CLOCK_HZ,
        /* Режим 0: такт в покое низкий, данные фиксируются по переднему фронту. */
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &device, &s_device));
}

static void dump_bytes(const char *title, const uint8_t *data, size_t length)
{
    char line[3 * 8 + 1];
    size_t pos = 0;
    for (size_t i = 0; i < length && pos + 4 < sizeof(line); i++) {
        pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%02X ", data[i]);
    }
    ESP_LOGI(TAG, "%s %s", title, line);
}

static void exchange_once(void)
{
    const uint8_t tx[] = {0xA5, 0x5A, 0x10, 0x01};
    uint8_t rx[sizeof(tx)];
    memset(rx, 0, sizeof(rx));

    spi_transaction_t transaction = {
        /*
         * Длина — в битах, не в байтах. Это частая ошибка:
         * четыре байта — это 32 бита, а не 4.
         */
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    ESP_ERROR_CHECK(spi_device_transmit(s_device, &transaction));

    dump_bytes("отправлено", tx, sizeof(tx));
    dump_bytes("принято   ", rx, sizeof(rx));
    if (memcmp(tx, rx, sizeof(tx)) == 0) {
        ESP_LOGI(TAG, "петля MOSI-MISO замкнута: принятое совпало с отправленным");
    } else {
        ESP_LOGI(TAG, "совпадения нет. Для петли соедините GPIO%d и GPIO%d",
                 PIN_MOSI, PIN_MISO);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "SPI2  %d Гц  mode 0", CLOCK_HZ);
    ESP_LOGI(TAG, "SCLK GPIO%d  MOSI GPIO%d  MISO GPIO%d  CS GPIO%d",
             PIN_SCLK, PIN_MOSI, PIN_MISO, PIN_CS);
    spi_setup();

    while (true) {
        exchange_once();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
