#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "esp_log.h"

#define I2C_SDA_GPIO 5
#define I2C_SCL_GPIO 6
#define OLED_ADDR    0x3C
#define BUTTON_PIN 9
int a=5;
// Очередь для передачи событий от прерывания к задаче
static QueueHandle_t gpio_evt_queue = NULL;

static const char *TAG = "U8G2_EXAMPLE";

static void IRAM_ATTR isr_handler(void* arg)
{
    // В прерывании нельзя делать долгие операции (printf, задержки).
    // Мы просто отправляем "1" в очередь, чтобы разбудить задачу.
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void button_task(void* arg)
{
    uint32_t io_num;
    while (1) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            printf("Button pressed! GPIO %lu\n", io_num);
            a = (a >= 5) ? 0 : a+1;
            vTaskDelay(pdMS_TO_TICKS(50)); //антидребезг
        }
    }
}

// --- Демонстрация текста (разные шрифты) ---
void demo_text(u8g2_t *u8g2) {
    u8g2_ClearBuffer(u8g2);
    
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr); // Маленький шрифт
    u8g2_DrawStr(u8g2, 0, 8, "Small font");
    
    u8g2_SetFont(u8g2, u8g2_font_6x12_tr); // Чуть больше
    u8g2_DrawStr(u8g2, 0, 22, "Medium 6x12");
    
    // Для 72x40 больше двух строк может не влезть, будьте осторожны
    u8g2_SendBuffer(u8g2);
    vTaskDelay(pdMS_TO_TICKS(3000));
}

// --- Демонстрация геометрических фигур ---
void demo_shapes(u8g2_t *u8g2) {
    u8g2_ClearBuffer(u8g2);
    
    // Рамка (Frame)
    u8g2_DrawFrame(u8g2, 2, 2, 68, 36);
    
    // Линия
    u8g2_DrawLine(u8g2, 5, 30, 65, 5);
    
    // Круг (Outline)
    u8g2_DrawCircle(u8g2, 36, 20, 10, U8G2_DRAW_ALL);
    
    // Закрашенный треугольник (упрощённо)
    u8g2_DrawTriangle(u8g2, 36, 5, 20, 35, 52, 35);
    
    u8g2_SendBuffer(u8g2);
    vTaskDelay(pdMS_TO_TICKS(3000));
}

// --- Демонстрация работы с пикселями ---
void demo_pixels(u8g2_t *u8g2) {
    u8g2_ClearBuffer(u8g2);
    
    // Рисуем "шахматный" узор из точек
    for (int x = 0; x < 72; x += 4) {
        for (int y = 0; y < 40; y += 4) {
            if ((x + y) % 8 == 0) {
                u8g2_DrawPixel(u8g2, x, y);
            }
        }
    }
    
    u8g2_SendBuffer(u8g2);
    vTaskDelay(pdMS_TO_TICKS(3000));
}

// --- Анимированный прогресс-бар ---
void demo_progress(u8g2_t *u8g2) {
    for (int progress = 0; progress <= 100; progress += 10) {
        u8g2_ClearBuffer(u8g2);
        
        // Рамка для бара
        u8g2_DrawFrame(u8g2, 2, 15, 68, 10);
        
        // Заполнение в зависимости от прогресса
        // 68 пикселей — это полная ширина, умножаем на progress/100
        int fill_width = (68 * progress) / 100;
        if (fill_width > 0) {
            u8g2_DrawBox(u8g2, 2, 15, fill_width, 10);
        }
        
        // Текст с процентами
        u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
        char buf[10];
        sprintf(buf, "%d%%", progress);
        u8g2_DrawStr(u8g2, 30, 38, buf);
        
        u8g2_SendBuffer(u8g2);
        vTaskDelay(pdMS_TO_TICKS(200)); // Пауза между кадрами
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// --- "Прыгающий мяч" (упрощённая физика) ---
void demo_bouncing_ball(u8g2_t *u8g2) {
    int x = 10, y = 10;
    int dx = 2, dy = 1;
    int radius = 3;
    
    for (int i = 0; i < 136; i++) { // 100 кадров анимации
        u8g2_ClearBuffer(u8g2);
        
        // Рисуем мяч
        u8g2_DrawDisc(u8g2, x, y, radius, U8G2_DRAW_ALL);
        
        // Обновляем позицию
        x += dx;
        y += dy;
        
        // Отскок от стен (учитывая радиус)
        if (x - radius <= 0 || x + radius >= 72) dx = -dx;
        if (y - radius <= 0 || y + radius >= 40) dy = -dy;
        
        u8g2_SendBuffer(u8g2);
        vTaskDelay(pdMS_TO_TICKS(50)); // 20 FPS
    }
}

// Пример простой иконки 8x8 (стрелка вверх или что-то простое)
// Каждый байт — это вертикальная полоска из 8 пикселей
static const uint8_t icon_arrow[] = {
    0x10, // 00010000
    0x38, // 00111000
    0x7C, // 01111100
    0xFE, // 11111110
    0x10, // 00010000
    0x10, // 00010000
    0x10, // 00010000
    0x00  // 00000000
};

void demo_bitmap(u8g2_t *u8g2) {
    u8g2_ClearBuffer(u8g2);
    u8g2_DrawXBMP(u8g2, 32, 16, 8, 8, icon_arrow);
    u8g2_SendBuffer(u8g2);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting U8g2 example");

    // 1. Конфигурация HAL под nltimv/u8g2_esp32_hal
    // Используем макрос U8G2_HAL_CONFIG_DEFAULT и переопределяем нужные поля
    u8g2_hal_config_t config = U8G2_HAL_CONFIG_DEFAULT;
    
    // Настройка I2C (поля внутри config.i2c)
    config.i2c.sda = I2C_SDA_GPIO;
    config.i2c.scl = I2C_SCL_GPIO;
    config.i2c.port = I2C_NUM_0;
    config.i2c.clk_speed = 400000;
    
    // Пин сброса не используется (GPIO_NUM_NC уже стоит по умолчанию)
    // config.gpio.rst = GPIO_NUM_NC;

    // 2. Инициализация HAL (принимает один аргумент - указатель на конфиг)
    if (u8g2_hal_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "HAL init failed");
        return;
    }

    // 3. Создаём объект u8g2
    u8g2_t u8g2;

    // 4. Настройка дисплея (профиль 72x40)
    // ВАЖНО: колбэки называются u8g2_esp32_hal_i2c_byte_cb и u8g2_esp32_hal_gpio_and_delay_cb
    u8g2_Setup_ssd1306_i2c_72x40_er_f(
        &u8g2,
        U8G2_R0,
        u8g2_esp32_hal_i2c_byte_cb,        // <-- правильное имя колбэка
        u8g2_esp32_hal_gpio_and_delay_cb   // <-- правильное имя колбэка
    );

    // 5. Установка I2C адреса (сдвиг влево на 1 бит)
    u8x8_SetI2CAddress(&u8g2.u8x8, OLED_ADDR << 1);

    // 6. Инициализация дисплея
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); // Включаем питание

    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_PIN); 
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE);
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, isr_handler, (void*) BUTTON_PIN);
    gpio_intr_enable(BUTTON_PIN);
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    // 7. Основной цикл
    while (1) {
        while (1) {
            switch(a)
            {
             case 0:   demo_text(&u8g2); break;
             case 1:   demo_shapes(&u8g2); break;
             case 2:   demo_pixels(&u8g2);break;
             case 3:   demo_progress(&u8g2); break;
             case 4:   demo_bitmap(&u8g2); break;
             case 5:   demo_bouncing_ball(&u8g2); break;
             default: break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}