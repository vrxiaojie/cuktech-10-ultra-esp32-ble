idf_component_register(
    SRCS
        "charger_state.c"
        "charger_state_core.c"
    INCLUDE_DIRS "include"
    REQUIRES
        cuktech_protocol
        freertos
)
