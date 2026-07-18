#include "app_config.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define APP_CONFIG_NVS_NAMESPACE "app_cfg"
#define APP_CONFIG_NVS_KEY "cfg_blob"

static const char *TAG = "app_config";

static void secure_zero(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (length-- > 0U) {
        *bytes++ = 0U;
    }
}

static esp_err_t status_to_esp_err(app_config_status_t status)
{
    switch (status) {
    case APP_CONFIG_STATUS_OK:
        return ESP_OK;
    case APP_CONFIG_STATUS_CORRUPT_BLOB:
        return ESP_ERR_INVALID_CRC;
    case APP_CONFIG_STATUS_UNSUPPORTED_SCHEMA:
        return ESP_ERR_INVALID_VERSION;
    case APP_CONFIG_STATUS_BUFFER_TOO_SMALL:
        return ESP_ERR_INVALID_SIZE;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t app_config_store_init(void)
{
    return nvs_flash_init();
}

esp_err_t app_config_save(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t blob[APP_CONFIG_BLOB_BUFFER_SIZE] = {0};
    size_t blob_len = 0;
    app_config_status_t status = app_config_encode(config, blob, sizeof(blob), &blob_len);
    if (status != APP_CONFIG_STATUS_OK) {
        secure_zero(blob, sizeof(blob));
        return status_to_esp_err(status);
    }

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(APP_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, APP_CONFIG_NVS_KEY, blob, blob_len);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (handle != 0U) {
        nvs_close(handle);
    }
    secure_zero(blob, sizeof(blob));
    return error;
}

esp_err_t app_config_load(app_config_t *config, bool *found)
{
    if (config == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    app_config_set_defaults(config);
    *found = false;

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(APP_CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    size_t blob_len = 0;
    error = nvs_get_blob(handle, APP_CONFIG_NVS_KEY, NULL, &blob_len);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (error != ESP_OK || blob_len == 0U || blob_len > app_config_blob_max_size()) {
        nvs_close(handle);
        return error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
    }

    uint8_t blob[APP_CONFIG_BLOB_BUFFER_SIZE] = {0};
    error = nvs_get_blob(handle, APP_CONFIG_NVS_KEY, blob, &blob_len);
    nvs_close(handle);
    if (error != ESP_OK) {
        secure_zero(blob, sizeof(blob));
        return error;
    }

    uint16_t source_schema = APP_CONFIG_SCHEMA_VERSION;
    app_config_status_t status = app_config_decode(blob, blob_len, config, &source_schema);
    secure_zero(blob, sizeof(blob));
    if (status != APP_CONFIG_STATUS_OK) {
        app_config_set_defaults(config);
        return status_to_esp_err(status);
    }
    *found = true;

    if (source_schema != APP_CONFIG_SCHEMA_VERSION) {
        ESP_LOGI(TAG, "migrating configuration schema %u to %u", source_schema,
                 APP_CONFIG_SCHEMA_VERSION);
        return app_config_save(config);
    }
    return ESP_OK;
}
