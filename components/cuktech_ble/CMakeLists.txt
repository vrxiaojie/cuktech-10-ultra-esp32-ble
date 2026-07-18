idf_component_register(
    SRCS
        "cuktech_ble.c"
        "cuktech_ble_core.c"
        "cuktech_miot_auth.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_config
        bt
        cuktech_protocol
        esp_hw_support
        esp_system
        freertos
)
