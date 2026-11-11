/*
 * Гибкое меню на OLED 72x40.
 *
 * Пункты описаны таблицей, а не цепочкой if. У пункта три вида:
 * подменю, действие и «назад». Добавить экран — значит дописать
 * строку в таблицу и, если нужно, одну функцию. Порядок пунктов
 * и вложенность меняются без переписывания цикла отрисовки.
 *
 * Кнопка одна, GPIO9, как в исходном проекте.
 * Короткое нажатие — следующий пункт, длинное (от 500 мс) — выбор.
 * На экране три строки, более длинный список прокручивается.
 */

#include <stdio.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

static const char *TAG = "menu";

#define OLED_ADDR 0x3C
#define BUTTON_GPIO GPIO_NUM_9
#define LONG_PRESS_US 500000
#define VISIBLE_ROWS 3
#define MENU_DEPTH 4

typedef enum {
    BTN_NONE = 0,
    BTN_SHORT,
    BTN_LONG,
} btn_event_t;

typedef struct menu_item menu_item_t;

typedef enum {
    ITEM_SUBMENU,
    ITEM_ACTION,
    ITEM_BACK,
} menu_kind_t;

struct menu_item {
    const char *title;
    menu_kind_t kind;
    const menu_item_t *children;
    int child_count;
    void (*action)(u8g2_t *u8g2);
};

typedef struct {
    const char *title;
    const menu_item_t *items;
    int count;
    int cursor;
} menu_frame_t;

static u8g2_t s_u8g2;
static int s_contrast = 128;

static void show_lines(u8g2_t *u8g2, const char *a, const char *b, const char *c)
{
    u8g2_ClearBuffer(u8g2);
    u8g2_DrawUTF8(u8g2, 0, 10, a);
    u8g2_DrawUTF8(u8g2, 0, 22, b);
    u8g2_DrawUTF8(u8g2, 0, 34, c);
    u8g2_SendBuffer(u8g2);
    vTaskDelay(pdMS_TO_TICKS(1200));
}

static void contrast_apply(u8g2_t *u8g2, int value)
{
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    s_contrast = value;
    u8g2_SetContrast(u8g2, (uint8_t)s_contrast);
}

static void action_contrast_up(u8g2_t *u8g2)
{
    contrast_apply(u8g2, s_contrast + 32);
    char line[20];
    snprintf(line, sizeof(line), "контраст %d", s_contrast);
    ESP_LOGI(TAG, "%s", line);
    show_lines(u8g2, "Экран", line, "");
}

static void action_contrast_down(u8g2_t *u8g2)
{
    contrast_apply(u8g2, s_contrast - 32);
    char line[20];
    snprintf(line, sizeof(line), "контраст %d", s_contrast);
    ESP_LOGI(TAG, "%s", line);
    show_lines(u8g2, "Экран", line, "");
}

static void action_contrast_reset(u8g2_t *u8g2)
{
    contrast_apply(u8g2, 128);
    ESP_LOGI(TAG, "контраст 128");
    show_lines(u8g2, "Экран", "контраст 128", "");
}

static void action_heap(u8g2_t *u8g2)
{
    char free_line[20];
    char min_line[20];
    snprintf(free_line, sizeof(free_line), "%" PRIu32, esp_get_free_heap_size());
    snprintf(min_line, sizeof(min_line), "мин %" PRIu32, esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "heap %s, %s", free_line, min_line);
    show_lines(u8g2, "Память, байт", free_line, min_line);
}

static void action_about(u8g2_t *u8g2)
{
    show_lines(u8g2, "ESP32-C3", "OLED 72x40", "кнопка GPIO9");
}

/* Таблица читается снизу вверх: подменю ссылается на уже описанный массив. */
static const menu_item_t SCREEN_ITEMS[] = {
    {"Контраст +", ITEM_ACTION, NULL, 0, action_contrast_up},
    {"Контраст -", ITEM_ACTION, NULL, 0, action_contrast_down},
    {"Сброс", ITEM_ACTION, NULL, 0, action_contrast_reset},
    {"Назад", ITEM_BACK, NULL, 0, NULL},
};

static const menu_item_t SYSTEM_ITEMS[] = {
    {"Память", ITEM_ACTION, NULL, 0, action_heap},
    {"О плате", ITEM_ACTION, NULL, 0, action_about},
    {"Назад", ITEM_BACK, NULL, 0, NULL},
};

static const menu_item_t ROOT_ITEMS[] = {
    {"Экран", ITEM_SUBMENU, SCREEN_ITEMS, sizeof(SCREEN_ITEMS) / sizeof(SCREEN_ITEMS[0]), NULL},
    {"Система", ITEM_SUBMENU, SYSTEM_ITEMS, sizeof(SYSTEM_ITEMS) / sizeof(SYSTEM_ITEMS[0]), NULL},
};

static bool display_init(void)
{
    u8g2_hal_config_t config = U8G2_HAL_CONFIG_DEFAULT;
    config.i2c.sda = GPIO_NUM_5;
    config.i2c.scl = GPIO_NUM_6;
    config.i2c.port = I2C_NUM_0;
    config.i2c.clk_speed = 400000;

    if (u8g2_hal_init(&config) != ESP_OK) {
        return false;
    }

    u8g2_Setup_ssd1306_i2c_72x40_er_f(
        &s_u8g2,
        U8G2_R0,
        u8g2_esp32_hal_i2c_byte_cb,
        u8g2_esp32_hal_gpio_and_delay_cb);
    /* u8g2 ждёт 8-битный адрес, 0x3C << 1 = 0x78. */
    u8x8_SetI2CAddress(&s_u8g2.u8x8, OLED_ADDR << 1);
    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    u8g2_SetFont(&s_u8g2, u8g2_font_5x8_t_cyrillic);
    u8g2_SetFontMode(&s_u8g2, 1);
    contrast_apply(&s_u8g2, s_contrast);
    return true;
}

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

/*
 * Опрос, а не прерывание: меню и так крутится в одной задаче.
 * Сначала ждём отпускания, чтобы длинное нажатие не превратилось
 * сразу во второе короткое.
 */
static btn_event_t button_poll(void)
{
    if (gpio_get_level(BUTTON_GPIO) != 0) {
        return BTN_NONE;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
    if (gpio_get_level(BUTTON_GPIO) != 0) {
        return BTN_NONE;
    }

    int64_t pressed_at = esp_timer_get_time();
    while (gpio_get_level(BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(30));

    int64_t held = esp_timer_get_time() - pressed_at;
    return held >= LONG_PRESS_US ? BTN_LONG : BTN_SHORT;
}

static void draw_menu(const menu_frame_t *frame)
{
    int start = 0;
    if (frame->cursor >= VISIBLE_ROWS) {
        start = frame->cursor - VISIBLE_ROWS + 1;
    }

    u8g2_ClearBuffer(&s_u8g2);
    u8g2_DrawUTF8(&s_u8g2, 0, 8, frame->title);
    u8g2_DrawHLine(&s_u8g2, 0, 10, 72);

    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int index = start + row;
        if (index >= frame->count) {
            break;
        }
        int baseline = 20 + row * 10;
        bool selected = index == frame->cursor;
        if (selected) {
            u8g2_SetDrawColor(&s_u8g2, 1);
            u8g2_DrawBox(&s_u8g2, 0, baseline - 7, 68, 9);
            u8g2_SetDrawColor(&s_u8g2, 0);
        }
        u8g2_DrawUTF8(&s_u8g2, 2, baseline, frame->items[index].title);
        u8g2_SetDrawColor(&s_u8g2, 1);
    }

    if (frame->count > VISIBLE_ROWS) {
        int track = 26;
        int bar = track * VISIBLE_ROWS / frame->count;
        if (bar < 3) {
            bar = 3;
        }
        int span = frame->count - VISIBLE_ROWS;
        int y = 12 + (track - bar) * start / span;
        u8g2_DrawBox(&s_u8g2, 70, y, 2, bar);
    }
    u8g2_SendBuffer(&s_u8g2);
}

static void menu_run(void)
{
    menu_frame_t stack[MENU_DEPTH];
    int depth = 0;
    stack[0] = (menu_frame_t){
        .title = "Меню",
        .items = ROOT_ITEMS,
        .count = (int)(sizeof(ROOT_ITEMS) / sizeof(ROOT_ITEMS[0])),
        .cursor = 0,
    };

    bool dirty = true;
    while (1) {
        if (dirty) {
            draw_menu(&stack[depth]);
            dirty = false;
        }

        btn_event_t event = button_poll();
        if (event == BTN_NONE) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        menu_frame_t *frame = &stack[depth];
        if (event == BTN_SHORT) {
            frame->cursor = (frame->cursor + 1) % frame->count;
            dirty = true;
            continue;
        }

        const menu_item_t *item = &frame->items[frame->cursor];
        if (item->kind == ITEM_SUBMENU && depth + 1 < MENU_DEPTH) {
            depth++;
            stack[depth] = (menu_frame_t){
                .title = item->title,
                .items = item->children,
                .count = item->child_count,
                .cursor = 0,
            };
        } else if (item->kind == ITEM_ACTION && item->action != NULL) {
            item->action(&s_u8g2);
        } else if (item->kind == ITEM_BACK && depth > 0) {
            depth--;
        }
        dirty = true;
    }
}

void app_main(void)
{
    button_init();
    if (!display_init()) {
        ESP_LOGE(TAG, "дисплей не открылся, проверьте SDA=5 SCL=6 и адрес 0x3C");
        return;
    }
    ESP_LOGI(TAG, "короткое нажатие — следующий пункт, длинное — выбор");
    menu_run();
}
