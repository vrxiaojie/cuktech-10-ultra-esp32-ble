cmake_minimum_required(VERSION 3.16)
project(cuktech_host_tests C)

enable_testing()

add_executable(app_config_tests
    test_app_config.c
    ../../components/app_config/app_config_core.c
)
target_include_directories(app_config_tests PRIVATE
    ../../components/app_config/include
)
target_compile_definitions(app_config_tests PRIVATE APP_CONFIG_TESTING=1)
target_compile_features(app_config_tests PRIVATE c_std_17)
target_compile_options(app_config_tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME app_config_tests COMMAND app_config_tests)

add_executable(cuktech_ble_core_tests
    test_cuktech_ble_core.c
    ../../components/cuktech_ble/cuktech_ble_core.c
)
target_include_directories(cuktech_ble_core_tests PRIVATE
    ../../components/cuktech_ble/include
)
target_compile_features(cuktech_ble_core_tests PRIVATE c_std_17)
target_compile_options(cuktech_ble_core_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME cuktech_ble_core_tests COMMAND cuktech_ble_core_tests)

add_executable(charger_state_tests
    test_charger_state.c
    ../../components/charger_state/charger_state_core.c
)
target_include_directories(charger_state_tests PRIVATE
    ../../components/charger_state/include
    ../../components/cuktech_protocol/include
)
target_compile_features(charger_state_tests PRIVATE c_std_17)
target_compile_options(charger_state_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(charger_state_tests PRIVATE m)
add_test(NAME charger_state_tests COMMAND charger_state_tests)

add_executable(mqtt_bridge_tests
    test_mqtt_bridge.c
    ../../components/mqtt_bridge/mqtt_bridge_model.c
    ../../components/charger_state/charger_state_core.c
    ../../components/cuktech_protocol/port_decode.c
)
target_include_directories(mqtt_bridge_tests PRIVATE
    ../../components/mqtt_bridge/include
    ../../components/charger_state/include
    ../../components/cuktech_protocol/include
)
target_compile_features(mqtt_bridge_tests PRIVATE c_std_17)
target_compile_options(mqtt_bridge_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(mqtt_bridge_tests PRIVATE m)
add_test(NAME mqtt_bridge_tests COMMAND mqtt_bridge_tests)

if(NOT IDF_PATH AND DEFINED ENV{IDF_PATH})
    set(IDF_PATH "$ENV{IDF_PATH}")
endif()

if(IDF_PATH AND EXISTS "${IDF_PATH}/components/json/cJSON/cJSON.c")
    add_executable(web_config_tests
        test_web_config.c
        ../../components/app_config/app_config_core.c
        ../../components/web_config/web_config_model.c
        ${IDF_PATH}/components/json/cJSON/cJSON.c
    )
    target_include_directories(web_config_tests PRIVATE
        ../../components/app_config/include
        ../../components/web_config/include
        ${IDF_PATH}/components/json/cJSON
    )
    target_compile_features(web_config_tests PRIVATE c_std_17)
    target_compile_options(web_config_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME web_config_tests COMMAND web_config_tests)
endif()

if(EXISTS "/usr/include/openssl/evp.h")
    add_executable(miot_auth_tests
        test_miot_auth.c
        crypto_openssl.c
        ../../components/cuktech_ble/cuktech_miot_auth.c
        ../../components/cuktech_protocol/cuktech_protocol.c
    )
    target_include_directories(miot_auth_tests PRIVATE
        ../../components/cuktech_ble/include
        ../../components/cuktech_protocol/include
        ../../components/cuktech_protocol
    )
    target_compile_features(miot_auth_tests PRIVATE c_std_17)
    target_compile_options(miot_auth_tests PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(miot_auth_tests PRIVATE crypto)
    add_test(NAME miot_auth_tests COMMAND miot_auth_tests)

    add_executable(command_tests
        test_command.c
        crypto_openssl.c
        ../../components/cuktech_ble/cuktech_command.c
        ../../components/cuktech_protocol/cuktech_protocol.c
    )
    target_include_directories(command_tests PRIVATE
        ../../components/cuktech_ble/include
        ../../components/cuktech_protocol/include
        ../../components/cuktech_protocol
    )
    target_compile_features(command_tests PRIVATE c_std_17)
    target_compile_options(command_tests PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(command_tests PRIVATE crypto)
    add_test(NAME command_tests COMMAND command_tests)

    add_executable(protocol_tests
        test_protocol.c
        crypto_openssl.c
        ../../components/cuktech_protocol/cuktech_protocol.c
        ../../components/cuktech_protocol/miot_tlv.c
        ../../components/cuktech_protocol/port_decode.c
    )
    target_include_directories(protocol_tests PRIVATE
        ../../components/cuktech_protocol/include
        ../../components/cuktech_protocol
    )
    target_compile_features(protocol_tests PRIVATE c_std_17)
    target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(protocol_tests PRIVATE crypto)
    add_test(NAME protocol_tests COMMAND protocol_tests)
endif()
