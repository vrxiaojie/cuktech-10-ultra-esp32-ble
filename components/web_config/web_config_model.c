#include "web_config_model.h"

#include <string.h>

#include "cJSON.h"

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *bytes = data;
    while (length-- > 0U) {
        *bytes++ = 0U;
    }
}

static void clear_json_secret(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        secure_zero(item->valuestring, strlen(item->valuestring));
    }
}

static bool read_bool(const cJSON *root, const char *name, bool *value, bool *present)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    *present = item != NULL;
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *value = cJSON_IsTrue(item);
    return true;
}

static bool read_string(const cJSON *root, const char *name, const char **value,
                        bool *present)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    *present = item != NULL;
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    *value = item->valuestring;
    return true;
}

static bool copy_string_field(const cJSON *root, const char *name, char *destination,
                              size_t destination_size, size_t *recognized)
{
    const char *value = NULL;
    bool present = false;
    if (!read_string(root, name, &value, &present)) {
        return false;
    }
    if (!present) {
        return true;
    }
    ++*recognized;
    size_t length = strlen(value);
    if (length >= destination_size) {
        return false;
    }
    memset(destination, 0, destination_size);
    memcpy(destination, value, length);
    return true;
}

static bool read_u16_field(const cJSON *root, const char *name, uint16_t *destination,
                           size_t *recognized)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (item == NULL) {
        return true;
    }
    ++*recognized;
    if (!cJSON_IsNumber(item) || item->valuedouble < 1.0 ||
        item->valuedouble > 65535.0) {
        return false;
    }
    uint32_t integer = (uint32_t)item->valuedouble;
    if ((double)integer != item->valuedouble) {
        return false;
    }
    *destination = (uint16_t)integer;
    return true;
}

static bool apply_binary_secret(const cJSON *root, const char *value_name,
                                const char *clear_name, uint8_t *secret,
                                size_t secret_size, bool *configured,
                                size_t *recognized)
{
    bool clear = false;
    bool clear_present = false;
    if (!read_bool(root, clear_name, &clear, &clear_present)) {
        return false;
    }
    const char *value = NULL;
    bool value_present = false;
    if (!read_string(root, value_name, &value, &value_present)) {
        return false;
    }
    *recognized += (clear_present ? 1U : 0U) + (value_present ? 1U : 0U);
    if (clear) {
        memset(secret, 0, secret_size);
        *configured = false;
        return true;
    }
    if (!value_present || value[0] == '\0') {
        return true;
    }
    return app_config_update_secret(value, strlen(value), false, secret, secret_size,
                                    configured) == APP_CONFIG_STATUS_OK;
}

static bool apply_mqtt_password(const cJSON *root, app_config_t *config,
                                size_t *recognized)
{
    bool clear = false;
    bool clear_present = false;
    if (!read_bool(root, "clear_mqtt_password", &clear, &clear_present)) {
        return false;
    }
    const char *password = NULL;
    bool password_present = false;
    if (!read_string(root, "mqtt_password", &password, &password_present)) {
        return false;
    }
    *recognized += (clear_present ? 1U : 0U) + (password_present ? 1U : 0U);
    if (clear) {
        memset(config->mqtt_password, 0, sizeof(config->mqtt_password));
        config->mqtt_password_configured = false;
        return true;
    }
    if (!password_present || password[0] == '\0') {
        return true;
    }
    size_t length = strlen(password);
    if (length > APP_CONFIG_MQTT_PASSWORD_MAX_LEN) {
        return false;
    }
    memset(config->mqtt_password, 0, sizeof(config->mqtt_password));
    memcpy(config->mqtt_password, password, length);
    config->mqtt_password_configured = true;
    return true;
}

web_config_model_status_t web_config_apply_json(const char *json, size_t json_len,
                                                app_config_t *config)
{
    if (json == NULL || config == NULL) {
        return WEB_CONFIG_MODEL_INVALID_ARGUMENT;
    }
    if (json_len == 0U || json_len > WEB_CONFIG_API_MAX_BODY_SIZE) {
        return WEB_CONFIG_MODEL_BODY_TOO_LARGE;
    }

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WEB_CONFIG_MODEL_INVALID_JSON;
    }

    app_config_t candidate = *config;
    size_t recognized = 0;
    const char *mac = NULL;
    bool mac_present = false;
    bool valid = read_string(root, "charger_mac", &mac, &mac_present);
    if (valid && mac_present) {
        ++recognized;
        valid = app_config_set_mac(&candidate, mac, strlen(mac)) == APP_CONFIG_STATUS_OK;
    }
    valid = valid && apply_binary_secret(root, "token", "clear_token", candidate.token,
                                         sizeof(candidate.token),
                                         &candidate.token_configured, &recognized);
    valid = valid && apply_binary_secret(root, "ble_key", "clear_ble_key",
                                         candidate.ble_key, sizeof(candidate.ble_key),
                                         &candidate.ble_key_configured, &recognized);
    valid = valid && copy_string_field(root, "mqtt_host", candidate.mqtt_host,
                                       sizeof(candidate.mqtt_host), &recognized);
    valid = valid && read_u16_field(root, "mqtt_port", &candidate.mqtt_port, &recognized);
    valid = valid && copy_string_field(root, "mqtt_username", candidate.mqtt_username,
                                       sizeof(candidate.mqtt_username), &recognized);
    valid = valid && apply_mqtt_password(root, &candidate, &recognized);
    valid = valid && copy_string_field(root, "mqtt_topic_prefix",
                                       candidate.mqtt_topic_prefix,
                                       sizeof(candidate.mqtt_topic_prefix), &recognized);
    valid = valid && read_u16_field(root, "mqtt_keepalive", &candidate.mqtt_keepalive,
                                    &recognized);

    bool ble_enabled = false;
    bool ble_enabled_present = false;
    valid = valid && read_bool(root, "ble_enabled", &ble_enabled, &ble_enabled_present);
    if (ble_enabled_present) {
        ++recognized;
        candidate.ble_enabled = ble_enabled;
    }
    clear_json_secret(root, "token");
    clear_json_secret(root, "ble_key");
    clear_json_secret(root, "mqtt_password");
    cJSON_Delete(root);

    if (!valid) {
        return WEB_CONFIG_MODEL_INVALID_FIELD;
    }
    if (recognized == 0U) {
        return WEB_CONFIG_MODEL_MISSING_FIELDS;
    }
    if (app_config_normalize(&candidate) != APP_CONFIG_STATUS_OK) {
        return WEB_CONFIG_MODEL_INVALID_CONFIG;
    }
    *config = candidate;
    return WEB_CONFIG_MODEL_OK;
}

web_config_model_status_t web_config_build_public_json(const app_config_t *config,
                                                       char *output,
                                                       size_t output_size)
{
    if (config == NULL || output == NULL || output_size == 0U) {
        return WEB_CONFIG_MODEL_INVALID_ARGUMENT;
    }
    app_config_public_t public_config;
    app_config_redact(config, &public_config);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return WEB_CONFIG_MODEL_OUTPUT_TOO_SMALL;
    }
    cJSON_AddNumberToObject(root, "schema_version", public_config.schema_version);
    cJSON_AddBoolToObject(root, "wifi_configured", public_config.wifi_configured);
    cJSON_AddStringToObject(root, "wifi_ssid", public_config.wifi_ssid);
    cJSON_AddStringToObject(root, "charger_mac", public_config.charger_mac);
    cJSON_AddBoolToObject(root, "token_configured", public_config.token_configured);
    cJSON_AddBoolToObject(root, "ble_key_configured", public_config.ble_key_configured);
    cJSON_AddStringToObject(root, "mqtt_host", public_config.mqtt_host);
    cJSON_AddNumberToObject(root, "mqtt_port", public_config.mqtt_port);
    cJSON_AddStringToObject(root, "mqtt_username", public_config.mqtt_username);
    cJSON_AddBoolToObject(root, "mqtt_password_configured",
                          public_config.mqtt_password_configured);
    cJSON_AddStringToObject(root, "mqtt_topic_prefix",
                           public_config.mqtt_topic_prefix);
    cJSON_AddNumberToObject(root, "mqtt_keepalive", public_config.mqtt_keepalive);
    cJSON_AddBoolToObject(root, "ble_enabled", public_config.ble_enabled);

    bool printed = cJSON_PrintPreallocated(root, output, (int)output_size, false);
    cJSON_Delete(root);
    return printed ? WEB_CONFIG_MODEL_OK : WEB_CONFIG_MODEL_OUTPUT_TOO_SMALL;
}

const char *web_config_model_status_name(web_config_model_status_t status)
{
    switch (status) {
    case WEB_CONFIG_MODEL_BODY_TOO_LARGE:
        return "body_too_large";
    case WEB_CONFIG_MODEL_INVALID_JSON:
        return "invalid_json";
    case WEB_CONFIG_MODEL_MISSING_FIELDS:
        return "missing_fields";
    case WEB_CONFIG_MODEL_INVALID_FIELD:
        return "invalid_field";
    case WEB_CONFIG_MODEL_INVALID_CONFIG:
        return "invalid_config";
    case WEB_CONFIG_MODEL_OUTPUT_TOO_SMALL:
        return "output_too_small";
    case WEB_CONFIG_MODEL_OK:
        return "ok";
    default:
        return "invalid_argument";
    }
}
