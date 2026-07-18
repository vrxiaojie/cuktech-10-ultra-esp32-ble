idf_component_register(
    SRCS
        "web_config.c"
        "web_config_model.c"
    INCLUDE_DIRS "include"
    REQUIRES
        charger_state
        cuktech_ble
        esp_http_server
        esp_system
        esp_app_format
        json
        wifi_manager
)
