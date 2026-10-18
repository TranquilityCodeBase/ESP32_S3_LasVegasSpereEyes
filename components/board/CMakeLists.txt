idf_component_register(
    SRCS "board_display.c" "board_backlight.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_i2c esp_driver_spi esp_driver_ledc esp_driver_gpio
)
