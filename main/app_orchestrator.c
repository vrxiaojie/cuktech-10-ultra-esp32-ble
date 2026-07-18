#include "app_orchestrator.h"

#include "app_config.h"
#include "cuktech_ble.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "web_config.h"
#include "wifi_manager.h"

static const char *TAG = "app";

void app_orchestrator_start(void)
{
    esp_chip_info_t chip_info = {0};
    const esp_app_desc_t *app_desc = esp_app_get_description();
    app_config_t config;
    bool config_found = false;

    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "CUKTECH BLE gateway starting");
    ESP_LOGI(TAG, "firmware=%s idf=%s", app_desc->version, app_desc->idf_ver);
    ESP_LOGI(TAG, "target=esp32c3 cores=%d revision=%d", chip_info.cores,
             chip_info.revision);
    ESP_LOGI(TAG, "free_heap=%lu", (unsigned long)esp_get_free_heap_size());

    esp_err_t error = app_config_store_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(error));
        return;
    }
    error = app_config_load(&config, &config_found);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "configuration load failed: %s; defaults kept in memory",
                 esp_err_to_name(error));
    } else {
        ESP_LOGI(TAG,
                 "configuration loaded: persisted=%s wifi=%s token=%s ble_key=%s mqtt_password=%s ble_enabled=%s",
                 config_found ? "yes" : "no", config.wifi_configured ? "yes" : "no",
                 config.token_configured ? "yes" : "no",
                 config.ble_key_configured ? "yes" : "no",
                 config.mqtt_password_configured ? "yes" : "no",
                 config.ble_enabled ? "yes" : "no");
    }
    error = wifi_manager_start(&config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi manager start failed: %s", esp_err_to_name(error));
        return;
    }
    error = web_config_start();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "provisioning HTTP server start failed: %s", esp_err_to_name(error));
        return;
    }
    error = cuktech_ble_start(&config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "BLE Central start failed: %s", esp_err_to_name(error));
    }
    ESP_LOGI(TAG, "orchestrator ready: wifi_state=%s",
             wifi_manager_state_name(wifi_manager_get_state()));
}
