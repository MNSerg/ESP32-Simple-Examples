/*
 * Пример 09. UART: принять байты и отправить их обратно.
 *
 * UART — асинхронный последовательный порт. Два провода данных:
 *   TX  — этот чип передаёт, собеседник принимает;
 *   RX  — этот чип принимает, собеседник передаёт;
 * плюс общая земля. Общего тактового провода нет, поэтому оба конца
 * заранее договариваются о скорости (здесь 115200 бит/с), числе бит
 * данных (8), чётности (нет) и числе стоп-битов (1). Эту четвёрку
 * коротко записывают «115200 8N1».
 *
 * На ESP32-C3 монитор `idf.py monitor` обычно сидит на встроенном USB,
 * а не на выводах UART. Сообщения ESP_LOGI вы увидите в мониторе.
 * Байты этого примера идут по другим выводам: GPIO21 (TX) и GPIO20 (RX).
 * Это вторая, независимая линия связи.
 *
 * Проверка без переходника: соедините GPIO21 с GPIO20 перемычкой.
 * Всё, что пример передаст, он же и примет. Это называется петля, loopback.
 *
 * Проверка с компьютером: USB-UART переходник, его RX на GPIO21, его TX
 * на GPIO20, земли вместе. Скорость в терминале — те же 115200 8N1.
 * Не подавайте на эти выводы уровни 5 В.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define UART_PORT UART_NUM_1
#define UART_TX GPIO_NUM_21
#define UART_RX GPIO_NUM_20
#define UART_BAUD 115200
#define RX_BUFFER_BYTES 1024

static const char *TAG = "uart";

static void uart_setup(void)
{
    const uart_config_t config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /*
     * uart_driver_install включает драйвер и выделяет буферы.
     * Размер приёмного буфера больше нуля обязателен, иначе читать нечего.
     * Размер передающего буфера 0 означает: uart_write_bytes отдаёт байты
     * сразу в периферию и возвращается, когда они приняты оборудованием.
     * Очередь событий UART нам не нужна, поэтому её длина 0 и указатель NULL.
     */
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, RX_BUFFER_BYTES, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX, UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void app_main(void)
{
    ESP_LOGI(TAG, "UART%d  %d 8N1, TX=GPIO%d RX=GPIO%d",
             UART_PORT, UART_BAUD, UART_TX, UART_RX);
    ESP_LOGI(TAG, "для петли соедините GPIO%d и GPIO%d", UART_TX, UART_RX);
    uart_setup();

    const char *hello = "\r\nESP32-C3 UART. Наберите текст, он вернётся обратно.\r\n";
    uart_write_bytes(UART_PORT, hello, strlen(hello));

    uint8_t buffer[128];
    while (true) {
        /*
         * uart_read_bytes ждёт не дольше указанного времени.
         * Если за 200 мс ничего не пришло, вернётся 0 — это не ошибка.
         */
        int received = uart_read_bytes(UART_PORT, buffer, sizeof(buffer) - 1,
                                       pdMS_TO_TICKS(200));
        if (received > 0) {
            buffer[received] = 0;
            ESP_LOGI(TAG, "принято %d байт: %s", received, (char *)buffer);
            const char *prefix = "echo: ";
            uart_write_bytes(UART_PORT, prefix, strlen(prefix));
            uart_write_bytes(UART_PORT, buffer, received);
        }
    }
}
