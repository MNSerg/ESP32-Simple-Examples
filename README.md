# ESP32-C3: учебные примеры для ESP-IDF

В каталоге `project-name` лежит исходный рабочий проект: OLED 72×40 по I2C и кнопка на GPIO9. Кнопка переключает демонстрации на экране (текст, фигуры, пиксели, полоска, картинка, прыгающий мяч).

Каталог `examples` — отдельные маленькие проекты вокруг того же чипа. Каждый собирается сам по себе, в коде комментарии на русском, рядом `README.md` с пояснением, схемой выводов и тем, что должно появиться в мониторе.

Примеры рассчитаны на **ESP32-C3** и **ESP-IDF 6.1**. На этом чипе одно ядро RISC-V, Wi-Fi 2.4 ГГц и только Bluetooth Low Energy. Классического Bluetooth, ЦАП и сенсорных площадок, которые встречаются в уроках про обычный ESP32, здесь нет.

## Что куда воткнуто в исходном проекте

| Сигнал | GPIO |
| --- | --- |
| I2C SDA дисплея | 5 |
| I2C SCL дисплея | 6 |
| Кнопка на землю, активный уровень 0 | 9 |
| Адрес OLED | `0x3C` |

Эти же выводы используются в примерах кнопки и сканера I2C, чтобы плату не перепаивать. Светодиод в новых примерах по умолчанию на GPIO8: на части плат он уже стоит, на остальных номер легко поменять в начале `main.c`.

## Порядок примеров

| Папка | Тема |
| --- | --- |
| [01_gpio_blink](examples/01_gpio_blink/README.md) | Выход GPIO, мигание светодиода |
| [02_button_poll](examples/02_button_poll/README.md) | Кнопка опросом и антидребезг |
| [03_button_interrupt](examples/03_button_interrupt/README.md) | Кнопка по прерыванию и очередь |
| [04_freertos_tasks](examples/04_freertos_tasks/README.md) | Задачи, очередь, мьютекс |
| [05_software_timer](examples/05_software_timer/README.md) | Программные таймеры FreeRTOS |
| [06_hardware_timers](examples/06_hardware_timers/README.md) | `esp_timer` и аппаратный GPTimer |
| [07_pwm_ledc](examples/07_pwm_ledc/README.md) | ШИМ, яркость светодиода |
| [08_adc](examples/08_adc/README.md) | АЦП, напряжение на GPIO4 |
| [09_uart](examples/09_uart/README.md) | UART, эхо байтов |
| [10_i2c_scanner](examples/10_i2c_scanner/README.md) | Шина I2C, поиск дисплея `0x3C` |
| [11_spi_master](examples/11_spi_master/README.md) | SPI-мастер |
| [12_wifi_station](examples/12_wifi_station/README.md) | Wi-Fi клиент роутера |
| [13_wifi_softap](examples/13_wifi_softap/README.md) | Своя точка доступа |
| [14_tcp_server](examples/14_tcp_server/README.md) | Передача байтов по TCP |
| [15_http_server](examples/15_http_server/README.md) | Страница в браузере |
| [16_ble_server](examples/16_ble_server/README.md) | BLE, чтение, запись, уведомления |
| [17_deep_sleep](examples/17_deep_sleep/README.md) | Глубокий сон |
| [18_light_sleep](examples/18_light_sleep/README.md) | Лёгкий сон |
| [19_nvs_storage](examples/19_nvs_storage/README.md) | Энергонезависимая память |
| [20_task_watchdog](examples/20_task_watchdog/README.md) | Сторожевой таймер задач |
| [21_temperature](examples/21_temperature/README.md) | Температура кристалла |
| [22_pm_auto_light_sleep](examples/22_pm_auto_light_sleep/README.md) | Автоматический лёгкий сон |
| [display/01_flexible_menu](examples/display/01_flexible_menu/README.md) | Меню на OLED через u8g2 |
| [display/02_system_status](examples/display/02_system_status/README.md) | Характеристики чипа на OLED |
| [23_pulse_stepper](examples/23_pulse_stepper/README.md) | Длительность импульса и шаговик DRV8833 |
| [24_i2c_transfer](examples/24_i2c_transfer/README.md) | Запись и чтение I2C, разбор ошибок |

Раздел дисплея собран отдельно: [examples/display/README.md](examples/display/README.md). Общий указатель с командами сборки: [examples/README.md](examples/README.md).

## Сборка любого примера

Нужен ESP-IDF 6.1 и один раз выполненная в этом терминале подготовка окружения:

```bash
. $HOME/esp/esp-idf/export.sh
cd examples/01_gpio_blink
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`set-target` создаёт локальный `sdkconfig` и нужен один раз на папку. Каталоги `build/` и `sdkconfig` в репозиторий не входят.

Порт `/dev/ttyACM0` — обычный USB ESP32-C3. Если система показала другой, подставьте его. Выход из монитора: `Ctrl+]`.

Исходный проект с дисплеем собирается так же, из каталога `project-name`. Ему и двум примерам в `examples/display` нужен доступ в сеть при первой сборке: компонент `u8g2` подтягивается из интернета. Остальные примеры внешних компонентов не требуют.
