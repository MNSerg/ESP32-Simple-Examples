/*
 * Пример 13. SoftAP: ESP32-C3 сам становится точкой доступа.
 *
 * SoftAP — «программная» точка доступа: чип раздаёт своё имя сети,
 * сам выдаёт адреса по DHCP и получает у себя адрес 192.168.4.1.
 * Домашний роутер для этого примера не нужен. Телефон или ноутбук
 * подключаются к сети ESP32C3-DEMO, пароль esp32c3demo.
 *
 * Станцией чип в этот момент не является: в интернет через него
 * уйти нельзя. Это отдельная маленькая сеть из платы и вашего телефона.
 * Роль станции была в примере 12. Одновременно STA и AP тоже возможны
 * (режим WIFI_MODE_APSTA), здесь оставлен только AP, чтобы было видно
 * одну роль.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define AP_SSID "ESP32C3-DEMO"
#define AP_PASS "esp32c3demo"
#define AP_CHANNEL 1
#define AP_MAX_CLIENTS 4

static const char *TAG = "softap";

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT) {
        return;
    }

    if (id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "точка \"%s\" поднята. Адрес платы 192.168.4.1", AP_SSID);
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = data;
        ESP_LOGI(TAG, "клиент " MACSTR " подключился, всего aid=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = data;
        ESP_LOGI(TAG, "клиент " MACSTR " отключился", MAC2STR(event->mac));
    }
}

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
    /* Интерфейс точки доступа. DHCP-сервер на нём включается сам. */
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));

    wifi_config_t config = {
        .ap = {
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CLIENTS,
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

void app_main(void)
{
    ESP_LOGI(TAG, "SSID %s, пароль %s, канал %d", AP_SSID, AP_PASS, AP_CHANNEL);
    nvs_setup();
    ap_start();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        wifi_sta_list_t clients = {0};
        if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) {
            ESP_LOGI(TAG, "сейчас подключено клиентов: %d", clients.num);
        }
    }
}
