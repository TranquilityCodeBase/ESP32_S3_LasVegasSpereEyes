idf_component_register(
    SRCS "eye_sync.c"
    INCLUDE_DIRS "include"
    REQUIRES eyes esp_wifi esp_netif esp_event nvs_flash esp_timer
)
