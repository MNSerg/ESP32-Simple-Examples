/*
 * Пример 16. BLE-сервер: плата объявляет себя, телефон читает и пишет байты.
 *
 * BLE (Bluetooth Low Energy) на ESP32-C3 — единственный вид Bluetooth.
 * Классического Bluetooth для наушников и последовательного SPP на этом
 * чипе нет. Телефон подключается не к «COM-порту», а к сервису GATT:
 * у сервиса есть характеристики, у характеристики есть значение.
 *
 * Роли:
 *   периферия (этот пример) — объявляет имя и ждёт подключения;
 *   центр (телефон с программой nRF Connect) — ищет и подключается.
 *
 * Сервис 0x00FF, две характеристики:
 *   0xFF01  чтение и запись одного байта. 0 выключает светодиод GPIO8,
 *           любое другое значение включает;
 *   0xFF02  чтение и уведомления. Раз в секунду, если телефон подписался,
 *           уходит счётчик uint32 в порядке little-endian.
 *
 * Короткие 16-битные UUID здесь только для учебы: их легко ввести в nRF Connect.
 * В изделии берут случайный 128-битный UUID, чтобы не пересечься с чужим профилем.
 *
 * Стек — NimBLE. Он стартует отдельной задачей nimble_host. Пока эта задача
 * не вызвала sync_cb, радио ещё не объявляет имя, и звать ble_gap_adv_start рано.
 */

#include <stdbool.h> /* Включены ли уведомления счётчика. */
#include <string.h>  /* memset обнуляет поля объявления, strlen считает имя. */

/*
 * Библиотеки ESP-IDF:
 * freertos/FreeRTOS.h и task.h — задача, которая раз в секунду шлёт счётчик.
 * driver/gpio.h — светодиод. Его байт пишет телефон в характеристику 0xFF01.
 * esp_log.h — журнал подключений.
 * nvs_flash.h — NimBLE хранит связки и настройки во flash. Без NVS стек не стартует.
 *
 * Библиотеки NimBLE, это и есть стек BLE:
 * nimble/nimble_port.h — инициализация порта: связать контроллер радио и хост.
 * nimble/nimble_port_freertos.h — запустить хост отдельной задачей FreeRTOS.
 * host/ble_hs.h — хост: события GAP, объявление имени, старт рекламы.
 * host/ble_uuid.h — макрос BLE_UUID16_DECLARE собирает 16-битный UUID.
 * host/ble_gatt.h — описание сервиса и характеристик, чтение и запись значения.
 * host/util/util.h — ble_hs_util_ensure_addr выбирает адрес, которым чип представляется.
 * services/gap/ble_svc_gap.h — стандартный сервис GAP, в нём лежит имя устройства.
 * services/gatt/ble_svc_gatt.h — стандартный сервис GATT, его требует стек.
 * os/os_mbuf.h — цепочка буферов. Телефон читает и пишет значение через неё,
 *   а не через обычный массив.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"

/* Прототип есть в исходниках NimBLE, но не в публичном заголовке.
 * Так же его объявляют штатные примеры ESP-IDF. */
void ble_store_config_init(void);

#define LED_GPIO GPIO_NUM_8
#define LED_ON_LEVEL 0

static const char *TAG = "ble";
static const char *DEVICE_NAME = "ESP32C3-BLE";

static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_led_value_handle;
static uint16_t s_counter_value_handle;
static uint8_t s_led_byte;
static volatile uint32_t s_counter;
static volatile bool s_counter_notify;

static void led_apply(uint8_t value)
{
    s_led_byte = value;
    bool on = value != 0;
    int level = on ? LED_ON_LEVEL : (1 - LED_ON_LEVEL);
    gpio_set_level(LED_GPIO, level);
    ESP_LOGI(TAG, "светодиод %s (байт %u)", on ? "включён" : "выключен", value);
}

/* Стек вызывает эту функцию, когда телефон читает или пишет характеристику. */
static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (attr_handle == s_led_value_handle) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            /* Телефон читает байт. Кладём его в буфер ответа стека. */
            int rc = os_mbuf_append(ctxt->om, &s_led_byte, sizeof(s_led_byte));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint8_t value = 0;
            uint16_t length = 0;
            /* Запись телефона приходит цепочкой буферов. Складываем её в один байт. */
            int rc = ble_hs_mbuf_to_flat(ctxt->om, &value, sizeof(value), &length);
            if (rc != 0 || length != 1) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            led_apply(value);
            return 0;
        }
    }

    if (attr_handle == s_counter_value_handle &&
        ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint32_t value = s_counter;
        int rc = os_mbuf_append(ctxt->om, &value, sizeof(value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY, /* Основной сервис, его телефон видит в списке. */
        .uuid = BLE_UUID16_DECLARE(0x00FF), /* Номер сервиса. В nRF Connect его вводят как 0x00FF. */
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(0xFF01),
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE, /* Телефону можно и читать, и писать. */
                .val_handle = &s_led_value_handle, /* Стек запишет сюда номер характеристики после регистрации. */
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFF02),
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, /* Чтение и подписка на уведомления. */
                .val_handle = &s_counter_value_handle,
            },
            {0}, /* Ноль заканчивает список характеристик. Без него стек читает память дальше. */
        },
    },
    {0}, /* Ноль заканчивает список сервисов. */
};

static int gap_event(struct ble_gap_event *event, void *arg);

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    /*
     * DISC_GEN — устройство видно при обычном поиске.
     * BREDR_UNSUP — классический Bluetooth не поддерживается, так и есть на C3.
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "объявление не собралось, rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    /* UND — подключение разрешено. GEN — общий, не направленный, поиск. */
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "объявление не стартовало, rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "ищем телефон. Имя в эфире: %s", DEVICE_NAME);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "телефон подключился, handle %d", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "подключение сорвалось, status=%d", event->connect.status);
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "телефон отключился, reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_counter_notify = false;
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_counter_value_handle) {
            s_counter_notify = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "уведомления счётчика: %s",
                     s_counter_notify ? "включены" : "выключены");
        }
        return 0;
    default:
        return 0;
    }
}

/* sync_cb вызывается один раз, когда контроллер и хост договорились. */
static void on_sync(void)
{
    /* 0 — публичный адрес не обязателен, можно взять случайный статический. */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "нет BLE-адреса, rc=%d", rc);
        return;
    }
    /* Узнать, каким типом адреса чип будет представляться в эфире, и сохранить его. */
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "не удалось выбрать адрес, rc=%d", rc);
        return;
    }
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "стек NimBLE сброшен, reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    /* nimble_port_run() не возвращается, пока стек не остановят. */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void notify_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_counter++;
        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_counter_notify) {
            continue;
        }
        uint32_t value = s_counter;
        /* Уведомление уходит не массивом, а буфером стека. Стек сам его освободит. */
        struct os_mbuf *buffer = ble_hs_mbuf_from_flat(&value, sizeof(value));
        if (buffer == NULL) {
            continue;
        }
        /* Отдать буфер конкретному телефону в характеристику счётчика. */
        int rc = ble_gatts_notify_custom(s_conn_handle, s_counter_value_handle, buffer);
        if (rc != 0) {
            ESP_LOGW(TAG, "notify rc=%d", rc);
        }
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
    led_apply(0);

    nvs_setup();
    /* Связать контроллер радио и хост. Объявлять имя ещё рано: стек не синхронизирован. */
    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = on_reset; /* Стек сам вызовет это, если его сбросят. */
    ble_hs_cfg.sync_cb = on_sync;   /* И это — когда хост и контроллер договорились. */
    ble_svc_gap_init();  /* Стандартный сервис с именем устройства. */
    ble_svc_gatt_init(); /* Стандартный сервис GATT. Без него свой сервис не зарегистрировать. */

    /* Посчитать, сколько памяти занять под таблицу атрибутов. Сами сервисы ещё не видны телефону. */
    int rc = ble_gatts_count_cfg(s_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg rc=%d", rc);
        return;
    }
    /* Положить сервисы в стек и заполнить val_handle каждой характеристики. */
    rc = ble_gatts_add_svcs(s_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs rc=%d", rc);
        return;
    }
    /* Имя, которое телефон покажет в списке найденных устройств. */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "имя устройства не установлено, rc=%d", rc);
        return;
    }
    /* Хранилище связок: куда стек запишет данные о сопряжении. */
    ble_store_config_init();

    /* Запустить задачу хоста. Реклама начнётся уже из on_sync, не отсюда. */
    nimble_port_freertos_init(host_task);
    xTaskCreate(notify_task, "notify", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "NimBLE запущен. Откройте nRF Connect и найдите %s", DEVICE_NAME);
}
