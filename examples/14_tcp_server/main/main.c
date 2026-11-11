/*
 * Пример 14. TCP-сервер поверх своей точки доступа.
 *
 * TCP — поток байтов с гарантией доставки: потерянный пакет стек
 * отправит ещё раз, получатель увидит байты в том же порядке.
 * Сервер ждёт входящие соединения на порту. Клиент (ноутбук) сам
 * открывает соединение на адрес сервера и этот порт.
 *
 * Чтобы пример не зависел от домашнего роутера, плата сначала
 * поднимает SoftAP из примера 13. Дальше с ноутбука, подключённого
 * к сети ESP32C3-DEMO:
 *
 *   nc 192.168.4.1 3333
 *
 * Наберите строку и нажмите Enter. Плата вернёт её с приставкой echo
 * и продублирует в монитор. Это тот же приём, что UART-эхо, только
 * байты идут по Wi-Fi.
 *
 * Сокеты в ESP-IDF — это LwIP с привычным интерфейсом BSD:
 * socket, bind, listen, accept, recv, send, close.
 */

#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"

#define AP_SSID "ESP32C3-DEMO"
#define AP_PASS "esp32c3demo"
#define TCP_PORT 3333

static const char *TAG = "tcp";

static void nvs_setup(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void ap_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    wifi_config_t config = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)config.ap.ssid, AP_SSID, sizeof(config.ap.ssid) - 1);
    strncpy((char *)config.ap.password, AP_PASS, sizeof(config.ap.password) - 1);
    config.ap.ssid_len = strlen(AP_SSID);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* Обслужить одного уже принятого клиента, пока он сам не закроет соединение. */
static void serve_client(int client)
{
    const char *hello = "ESP32-C3 TCP. Наберите строку.\r\n";
    send(client, hello, strlen(hello), 0);

    char buffer[128];
    while (true) {
        int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received < 0) {
            ESP_LOGW(TAG, "recv: errno %d", errno);
            break;
        }
        if (received == 0) {
            ESP_LOGI(TAG, "клиент закрыл соединение");
            break;
        }
        buffer[received] = 0;
        ESP_LOGI(TAG, "принято: %s", buffer);
        send(client, "echo: ", 6, 0);
        send(client, buffer, received, 0);
    }
    close(client);
}

static void tcp_server_task(void *arg)
{
    (void)arg;

    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_fd < 0) {
        ESP_LOGE(TAG, "socket() не создал дескриптор, errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ESP_LOGE(TAG, "bind порта %d не удался, errno %d", TCP_PORT, errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }
    /* Один клиент в очереди ожидания. Сразу больше одного пример не обслуживает. */
    if (listen(listen_fd, 1) != 0) {
        ESP_LOGE(TAG, "listen не удался, errno %d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "слушаю %d. С ноутбука в сети %s:  nc 192.168.4.1 %d",
             TCP_PORT, AP_SSID, TCP_PORT);

    while (true) {
        struct sockaddr_in peer = {0};
        socklen_t peer_len = sizeof(peer);
        /*
         * accept блокирует задачу, пока никто не подключился.
         * Остальные задачи FreeRTOS в это время работают.
         */
        int client = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
        if (client < 0) {
            ESP_LOGW(TAG, "accept: errno %d", errno);
            continue;
        }
        ESP_LOGI(TAG, "соединение с %s", inet_ntoa(peer.sin_addr));
        serve_client(client);
    }
}

void app_main(void)
{
    nvs_setup();
    ap_start();
    /*
     * Стек 4096 байт: внутри задачи LwIP и форматирование лога.
     * Приоритет чуть выше main, чтобы приём байтов не ждал печати статуса.
     */
    xTaskCreate(tcp_server_task, "tcp", 4096, NULL, 5, NULL);
}
