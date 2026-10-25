idf_component_register(
    SRCS "ld2450.c"
    INCLUDE_DIRS "include"
    REQUIRES eyes esp_driver_uart esp_driver_gpio freertos esp_timer
    PRIV_REQUIRES log
)
