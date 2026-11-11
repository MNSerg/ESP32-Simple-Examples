/*
 * Пример 05. Программные таймеры FreeRTOS.
 *
 * Программный таймер — это не отдельная периферия чипа и не отдельная
 * задача на каждый интервал. Все такие таймеры обслуживает одна системная
 * задача, её обычно зовут Tmr Svc. Когда интервал истекает, эта задача
 * вызывает вашу функцию.
 *
 * Отсюда два следствия:
 *   1. Колбэк таймера не должен долго работать и не должен брать мьютекс,
 *      который может держать кто-то ещё. Пока колбэк не вернулся,
 *      остальные программные таймеры не обслуживаются.
 *   2. Точность — порядка одного тика FreeRTOS (обычно 10 мс), не микросекунды.
 *      Для микросекунд есть esp_timer и аппаратный GPTimer, пример 06.
 *
 * Здесь два таймера:
 *   periodic — срабатывает каждую секунду и увеличивает счётчик;
 *   oneshot  — один раз, через 5 секунд после старта.
 */

/*
 * Библиотеки:
 * freertos/FreeRTOS.h — типы планировщика, подключается до task.h и timers.h.
 * freertos/task.h — vTaskDelay в задаче main, чтобы она не крутилась без паузы.
 * freertos/timers.h — программные таймеры: xTimerCreate и xTimerStart.
 *   Это не аппаратный таймер чипа, а служба внутри FreeRTOS.
 * esp_log.h — печать из колбэка таймера и из main.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"

static const char *TAG = "sw_timer";

/*
 * volatile: переменную пишут в одной задаче, а читают в другой.
 * Без volatile компилятор имеет право закэшировать старое значение
 * в регистре и не увидеть новые тики.
 */
static volatile int s_ticks;

static void periodic_callback(TimerHandle_t timer)
{
    (void)timer;
    s_ticks++;
    /*
     * ESP_LOGI из колбэка программного таймера допустим, если сообщение короткое.
     * Долгую работу (сеть, запись во flash, ожидание кнопки) отсюда запускать нельзя:
     * лучше дать семафор отдельной задаче.
     */
    ESP_LOGI(TAG, "периодический таймер, тик %d", s_ticks);
}

static void oneshot_callback(TimerHandle_t timer)
{
    (void)timer;
    ESP_LOGW(TAG, "одноразовый таймер сработал. Повторно сам он не запустится");
}

void app_main(void)
{
    ESP_LOGI(TAG, "запуск двух программных таймеров");

    /*
     * Аргументы xTimerCreate:
     *   имя          — видно в отладчике, на работу не влияет;
     *   период       — в тиках, не в миллисекундах;
     *   pdTRUE       — периодический, pdFALSE — один раз;
     *   id           — произвольное число, его можно прочитать в колбэке;
     *   функция      — кого вызвать по истечении периода.
     *
     * Таймер создаётся остановленным. Время пойдёт только после xTimerStart.
     */
    TimerHandle_t periodic = xTimerCreate(
        "period", pdMS_TO_TICKS(1000), pdTRUE, (void *)1, periodic_callback);
    TimerHandle_t oneshot = xTimerCreate(
        "once", pdMS_TO_TICKS(5000), pdFALSE, (void *)2, oneshot_callback);

    if (periodic == NULL || oneshot == NULL) {
        ESP_LOGE(TAG, "таймер не создан. Часто это значит, что не хватило кучи");
        return;
    }

    /*
     * Второй аргумент xTimerStart — сколько тиков ждать, если очередь
     * команды таймерной задаче занята. 0 означает «не ждать»: для старта
     * из app_main очередь пуста, команда помещается сразу.
     */
    if (xTimerStart(periodic, 0) != pdPASS || xTimerStart(oneshot, 0) != pdPASS) {
        ESP_LOGE(TAG, "не удалось запустить таймер");
        return;
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "main видит %d тиков периодического таймера", s_ticks);
    }
}
