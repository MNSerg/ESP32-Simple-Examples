/*
 * Пример 19. NVS: записать число и строку так, чтобы они пережили выключение.
 *
 * NVS (Non-Volatile Storage) — ключ-значение во flash.
 * В отличие от обычной переменной, запись остаётся после снятия питания.
 * В отличие от RTC_DATA_ATTR из примера глубокого сна, она остаётся
 * и после полного обесточивания, не только после сна.
 *
 * Wi-Fi и BLE из предыдущих примеров уже вызывали nvs_flash_init():
 * драйвер радио хранит там свою калибровку. Здесь то же хранилище
 * используется для своих данных.
 *
 * Устройство хранилища:
 *   раздел flash «nvs» в таблице разделов;
 *   внутри него пространства имён (namespace), до 15 символов;
 *   внутри пространства ключи, тоже до 15 символов.
 * Имя пространства и ключ — строки. Значение — целое, строка или набор байт.
 *
 * nvs_set_* кладёт значение в оперативный кэш раздела.
 * На флеш оно попадает в nvs_commit. Без commit выключение сотрёт
 * то, что вы «уже записали».
 */

#include <inttypes.h> /* PRId32 печатает число загрузок типа int32_t. */
#include <stdio.h>    /* snprintf собирает текст заметки. */

/*
 * Библиотеки:
 * esp_log.h — печать прочитанных значений.
 * nvs.h — открыть пространство имён, прочитать и записать ключ, сбросить кэш на flash.
 * nvs_flash.h — подготовить сам раздел flash. nvs.h работает уже внутри него.
 */
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nvs";
static const char *NAMESPACE = "storage";

static void nvs_setup(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "раздел NVS надо стереть (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    nvs_setup();

    nvs_handle_t handle;
    /*
     * nvs_open открывает пространство «storage» и на чтение, и на запись.
     * Ручка handle дальше заменяет имя пространства во всех вызовах.
     */
    ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &handle));

    int32_t boots = 0;
    /* Прочитать прошлое значение. Ключа нет только при самом первом запуске. */
    esp_err_t err = nvs_get_i32(handle, "boots", &boots);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "ключ boots ещё не создан, начинаю с нуля");
        boots = 0;
    } else {
        ESP_ERROR_CHECK(err);
    }
    boots++;
    /* set кладёт значение в кэш. На flash оно попадёт только в nvs_commit ниже. */
    ESP_ERROR_CHECK(nvs_set_i32(handle, "boots", boots));

    /*
     * Строка хранится вместе с завершающим нулём, если пользоваться
     * nvs_set_str / nvs_get_str. Длина буфера на чтение — это и вход
     * (сколько места есть), и выход (сколько байт заняла строка).
     */
    char note[32];
    size_t note_length = sizeof(note);
    err = nvs_get_str(handle, "note", note, &note_length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "ключ note ещё не создан");
        note[0] = 0;
    } else {
        ESP_ERROR_CHECK(err);
        ESP_LOGI(TAG, "прошлая заметка: \"%s\"", note);
    }

    snprintf(note, sizeof(note), "boot %" PRId32, boots);
    ESP_ERROR_CHECK(nvs_set_str(handle, "note", note));

    /*
     * commit сбрасывает кэш этого пространства на flash.
     * Имеет смысл копить несколько set и вызывать commit один раз:
     * флеш изнашивается от числа стираний, а не от числа переменных в программе.
     */
    ESP_ERROR_CHECK(nvs_commit(handle));
    /* close отпускает ручку. Данные уже во flash, ручка больше не нужна. */
    nvs_close(handle);

    ESP_LOGI(TAG, "загрузка %" PRId32 " записана. Заметка теперь \"%s\"", boots, note);
    ESP_LOGI(TAG, "нажмите кнопку RESET или снимите питание: число увеличится на 1");
}
