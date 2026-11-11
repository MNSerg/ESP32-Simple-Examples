/*
 * Пример 15. HTTP-сервер: страница в браузере телефона.
 *
 * HTTP поверх TCP выглядит как текст. Браузер подключается к порту 80
 * и присылает запрос:
 *
 *   GET /click HTTP/1.1
 *   Host: 192.168.4.1
 *
 * Сервер отвечает строкой состояния и телом страницы. Разбирать этот
 * текст вручную не нужно: компонент esp_http_server принимает соединение,
 * находит путь и вызывает вашу функцию.
 *
 * Плата снова раздаёт сеть ESP32C3-DEMO / esp32c3demo.
 * В браузере телефона, подключённого к этой сети, откройте:
 *
 *   http://192.168.4.1/
 *
 * Ссылки на странице увеличивают счётчик и включают или выключают
 * светодиод на GPIO8. Это уже не «просто байты», как в TCP-эхо,
 * а маленький интерфейс.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define AP_SSID "ESP32C3-DEMO"
#define AP_PASS "esp32c3demo"
#define LED_GPIO GPIO_NUM_8
#define LED_ON_LEVEL 0

static const char *TAG = "http";
static int s_clicks;
static bool s_led_on;

static void led_set(bool on)
{
    s_led_on = on;
    int level = on ? LED_ON_LEVEL : (1 - LED_ON_LEVEL);
    gpio_set_level(LED_GPIO, level);
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

/* Общая страница. Её отдают и «/», и действия, чтобы браузер сразу показал итог. */
static esp_err_t send_page(httpd_req_t *request)
{
    char page[640];
    int length = snprintf(
        page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>ESP32-C3</title></head><body>"
        "<h1>ESP32-C3</h1>"
        "<p>Нажатий: %d</p>"
        "<p>Светодиод: %s</p>"
        "<p><a href=\"/click\">Нажать</a></p>"
        "<p><a href=\"/led?on=1\">Светодиод включить</a></p>"
        "<p><a href=\"/led?on=0\">Светодиод выключить</a></p>"
        "</body></html>",
        s_clicks, s_led_on ? "включён" : "выключен");

    if (length < 0 || length >= (int)sizeof(page)) {
        return httpd_resp_send_500(request);
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, page, length);
}

static esp_err_t root_get(httpd_req_t *request)
{
    return send_page(request);
}

static esp_err_t click_get(httpd_req_t *request)
{
    s_clicks++;
    ESP_LOGI(TAG, "клик %d", s_clicks);
    return send_page(request);
}

static esp_err_t led_get(httpd_req_t *request)
{
    char query[32];
    char value[8];
    /* Строка запроса — это часть URL после '?'. У «/» её нет, у «/led?on=1» есть. */
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "on", value, sizeof(value)) == ESP_OK) {
        led_set(value[0] == '1');
        ESP_LOGI(TAG, "светодиод %s", s_led_on ? "включён" : "выключен");
    }
    return send_page(request);
}

static void http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get,
    };
    const httpd_uri_t click = {
        .uri = "/click",
        .method = HTTP_GET,
        .handler = click_get,
    };
    const httpd_uri_t led = {
        .uri = "/led",
        .method = HTTP_GET,
        .handler = led_get,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &click));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &led));
    ESP_LOGI(TAG, "откройте http://192.168.4.1/ в сети %s", AP_SSID);
}

void app_main(void)
{
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led));
    led_set(false);

    nvs_setup();
    ap_start();
    http_start();
}
