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
