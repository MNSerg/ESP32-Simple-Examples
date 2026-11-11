idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES u8g2_esp32_hal esp_driver_gpio freertos esp_timer esp_system
)
