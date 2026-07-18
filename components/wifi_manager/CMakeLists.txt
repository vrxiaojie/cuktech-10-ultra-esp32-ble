idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_config
        esp_event
        esp_netif
        esp_timer
        esp_wifi
)
