idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    # esp_driver_gpio — управление выводами.
    # freertos — vTaskDelay и типы TickType_t.
    # В ESP-IDF 6 драйверы больше не подтягиваются «пакетом» через компонент driver,
    # каждый драйвер нужно указывать отдельно.
    REQUIRES esp_driver_gpio freertos
)
