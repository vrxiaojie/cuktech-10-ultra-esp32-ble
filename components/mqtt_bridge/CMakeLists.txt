idf_component_register(
    SRCS
        "mqtt_bridge.c"
        "mqtt_bridge_model.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_config
        charger_state
        esp_event
        freertos
        mqtt
)
