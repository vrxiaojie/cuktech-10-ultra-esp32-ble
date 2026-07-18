#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_CONFIG_SCHEMA_VERSION 1U
#define APP_CONFIG_WIFI_SSID_MAX_LEN 32U
#define APP_CONFIG_WIFI_PASSWORD_MAX_LEN 64U
#define APP_CONFIG_MAC_TEXT_LEN 17U
#define APP_CONFIG_TOKEN_LEN 12U
#define APP_CONFIG_BLE_KEY_LEN 16U
#define APP_CONFIG_MQTT_HOST_MAX_LEN 128U
#define APP_CONFIG_MQTT_USERNAME_MAX_LEN 64U
#define APP_CONFIG_MQTT_PASSWORD_MAX_LEN 64U
#define APP_CONFIG_MQTT_TOPIC_MAX_LEN 128U
#define APP_CONFIG_DEFAULT_MQTT_PORT 1883U
#define APP_CONFIG_DEFAULT_MQTT_KEEPALIVE 60U
#define APP_CONFIG_DEFAULT_TOPIC_PREFIX "cuktech/charger"
#define APP_CONFIG_BLOB_BUFFER_SIZE 1024U

typedef enum {
    APP_CONFIG_STATUS_OK = 0,
    APP_CONFIG_STATUS_INVALID_ARGUMENT,
    APP_CONFIG_STATUS_INVALID_STRING,
    APP_CONFIG_STATUS_INVALID_WIFI,
    APP_CONFIG_STATUS_INVALID_MAC,
    APP_CONFIG_STATUS_INVALID_HEX,
    APP_CONFIG_STATUS_INVALID_MQTT,
    APP_CONFIG_STATUS_BUFFER_TOO_SMALL,
    APP_CONFIG_STATUS_CORRUPT_BLOB,
    APP_CONFIG_STATUS_UNSUPPORTED_SCHEMA,
} app_config_status_t;

typedef struct {
    uint16_t schema_version;
    bool wifi_configured;
    char wifi_ssid[APP_CONFIG_WIFI_SSID_MAX_LEN + 1U];
    char wifi_password[APP_CONFIG_WIFI_PASSWORD_MAX_LEN + 1U];
    char charger_mac[APP_CONFIG_MAC_TEXT_LEN + 1U];
    uint8_t token[APP_CONFIG_TOKEN_LEN];
    bool token_configured;
    uint8_t ble_key[APP_CONFIG_BLE_KEY_LEN];
    bool ble_key_configured;
    char mqtt_host[APP_CONFIG_MQTT_HOST_MAX_LEN + 1U];
    uint16_t mqtt_port;
    char mqtt_username[APP_CONFIG_MQTT_USERNAME_MAX_LEN + 1U];
    char mqtt_password[APP_CONFIG_MQTT_PASSWORD_MAX_LEN + 1U];
    bool mqtt_password_configured;
    char mqtt_topic_prefix[APP_CONFIG_MQTT_TOPIC_MAX_LEN + 1U];
    uint16_t mqtt_keepalive;
    bool ble_enabled;
} app_config_t;

typedef struct {
    uint16_t schema_version;
    bool wifi_configured;
    char wifi_ssid[APP_CONFIG_WIFI_SSID_MAX_LEN + 1U];
    char charger_mac[APP_CONFIG_MAC_TEXT_LEN + 1U];
    bool token_configured;
    bool ble_key_configured;
    char mqtt_host[APP_CONFIG_MQTT_HOST_MAX_LEN + 1U];
    uint16_t mqtt_port;
    char mqtt_username[APP_CONFIG_MQTT_USERNAME_MAX_LEN + 1U];
    bool mqtt_password_configured;
    char mqtt_topic_prefix[APP_CONFIG_MQTT_TOPIC_MAX_LEN + 1U];
    uint16_t mqtt_keepalive;
    bool ble_enabled;
} app_config_public_t;

void app_config_set_defaults(app_config_t *config);
app_config_status_t app_config_normalize(app_config_t *config);
app_config_status_t app_config_validate(const app_config_t *config);
app_config_status_t app_config_set_mac(app_config_t *config, const char *input,
                                       size_t input_len);
app_config_status_t app_config_parse_hex(const char *input, size_t input_len,
                                         uint8_t *output, size_t output_len);
app_config_status_t app_config_update_secret(const char *input, size_t input_len,
                                             bool clear, uint8_t *secret,
                                             size_t secret_len, bool *configured);
void app_config_redact(const app_config_t *config, app_config_public_t *public_config);

size_t app_config_blob_max_size(void);
app_config_status_t app_config_encode(const app_config_t *config, uint8_t *blob,
                                      size_t blob_capacity, size_t *blob_len);
app_config_status_t app_config_decode(const uint8_t *blob, size_t blob_len,
                                      app_config_t *config, uint16_t *source_schema);

#ifdef APP_CONFIG_TESTING
app_config_status_t app_config_test_encode_legacy_v0(const app_config_t *config,
                                                     uint8_t *blob,
                                                     size_t blob_capacity,
                                                     size_t *blob_len);
#endif
