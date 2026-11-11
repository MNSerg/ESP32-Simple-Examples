/*
 * Пример 12. Wi-Fi станция: подключиться к роутеру и получить IP.
 *
 * Станция (STA) — это роль «клиент». Роутер в этой паре называется
 * точкой доступа (AP). ESP32-C3 умеет 802.11 b/g/n на 2.4 ГГц и не
 * умеет диапазон 5 ГГц. Роутер, раздающий только 5 ГГц, чип не увидит.
 *
 * Сетевой код ESP-IDF устроен событиями. Драйвер Wi-Fi не возвращает
 * «подключился» из функции esp_wifi_connect(). Он позже присылает
 * событие WIFI_EVENT_STA_CONNECTED, а ещё позже — IP_EVENT_STA_GOT_IP,
 * когда DHCP-сервер роутера выдал адрес. Пока адреса нет, сокетами
 * пользоваться рано.
 *
 * Обработчик события должен быть коротким. Ждать внутри него нельзя:
 * им пользуется системная задача событий. Результат здесь передаётся
 * в задачу main через группу событий FreeRTOS — набор битов, которых
 * можно ждать.
 *
 * Перед первым запуском впишите имя и пароль своей сети в WIFI_SSID
 * и WIFI_PASS ниже. Пароль WPA2 не короче 8 символов.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAX_RETRY 5

static const char *TAG = "wifi_sta";

static EventGroupHandle_t s_wifi_events;
static int s_retry_count;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        /* Драйвер поднялся. Теперь можно просить подключение. */
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "нет соединения, попытка %d", s_retry_count);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        s_retry_count = 0;
        ESP_LOGI(TAG, "адрес " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void nvs_setup(void)
{
    /*
     * Wi-Fi хранит калибровку радио во flash через NVS.
     * Без инициализации NVS esp_wifi_init вернёт ошибку.
     * ESP_ERR_NVS_NO_FREE_PAGES бывает после смены таблицы разделов:
     * тогда раздел стирают и открывают заново.
     */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* Сетевой интерфейс станции: через него потом пойдёт TCP/IP. */
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t config = {0};
    strncpy((char *)config.sta.ssid, WIFI_SSID, sizeof(config.sta.ssid) - 1);
    strncpy((char *)config.sta.password, WIFI_PASS, sizeof(config.sta.password) - 1);
    /*
     * Порог аутентификации: не подключаться к открытой сети, если мы
     * рассчитываем на WPA2. Для открытой сети порог ставят WIFI_AUTH_OPEN
     * и оставляют пароль пустым.
     */
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    /*
     * WIFI_PS_MIN_MODEM — модемный сон: между пакетами маяка радио засыпает,
     * процессор при желании тоже может спать. Это основной способ снизить
     * потребление, не отключая Wi-Fi совсем. Подробнее в примерах сна.
     */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    ESP_LOGI(TAG, "подключение к сети \"%s\"", WIFI_SSID);
    nvs_setup();
    wifi_start();

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "станция в сети. Дальше можно открывать сокеты");
    } else {
        ESP_LOGE(TAG, "не удалось подключиться. Проверьте SSID, пароль и диапазон 2.4 ГГц");
    }

    while (true) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "роутер \"%s\", канал %u, RSSI %d дБм",
                     (char *)ap.ssid, ap.primary, ap.rssi);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
