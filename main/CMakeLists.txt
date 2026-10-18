idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES board eyes ld2450 eye_sync nvs_flash esp_timer
)
