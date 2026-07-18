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
