idf_component_register(
    SRCS "web_config.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server
        esp_system
        json
        wifi_manager
)
