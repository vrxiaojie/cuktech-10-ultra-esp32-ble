idf_component_register(
    SRCS
        "cuktech_ble.c"
        "cuktech_ble_core.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_config
        bt
        esp_system
        freertos
)
