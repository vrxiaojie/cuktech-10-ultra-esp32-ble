#include "app_config_core.h"

#include <ctype.h>
#include <string.h>

#define APP_CONFIG_BLOB_MAGIC 0x43474643UL

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t payload_size;
    uint32_t payload_crc32;
} app_config_blob_header_t;

typedef struct __attribute__((packed)) {
    uint8_t wifi_configured;
    char wifi_ssid[APP_CONFIG_WIFI_SSID_MAX_LEN + 1U];
    char wifi_password[APP_CONFIG_WIFI_PASSWORD_MAX_LEN + 1U];
    char mqtt_host[APP_CONFIG_MQTT_HOST_MAX_LEN + 1U];
    uint16_t mqtt_port;
    char mqtt_username[APP_CONFIG_MQTT_USERNAME_MAX_LEN + 1U];
    char mqtt_password[APP_CONFIG_MQTT_PASSWORD_MAX_LEN + 1U];
    uint8_t mqtt_password_configured;
} app_config_payload_v0_t;

typedef struct __attribute__((packed)) {
    uint8_t wifi_configured;
    char wifi_ssid[APP_CONFIG_WIFI_SSID_MAX_LEN + 1U];
    char wifi_password[APP_CONFIG_WIFI_PASSWORD_MAX_LEN + 1U];
    char charger_mac[APP_CONFIG_MAC_TEXT_LEN + 1U];
    uint8_t token[APP_CONFIG_TOKEN_LEN];
    uint8_t token_configured;
    uint8_t ble_key[APP_CONFIG_BLE_KEY_LEN];
    uint8_t ble_key_configured;
    char mqtt_host[APP_CONFIG_MQTT_HOST_MAX_LEN + 1U];
    uint16_t mqtt_port;
    char mqtt_username[APP_CONFIG_MQTT_USERNAME_MAX_LEN + 1U];
    char mqtt_password[APP_CONFIG_MQTT_PASSWORD_MAX_LEN + 1U];
    uint8_t mqtt_password_configured;
    char mqtt_topic_prefix[APP_CONFIG_MQTT_TOPIC_MAX_LEN + 1U];
    uint16_t mqtt_keepalive;
    uint8_t ble_enabled;
} app_config_payload_v1_t;

static bool bounded_string_length(const char *value, size_t capacity, size_t *length)
{
    const char *terminator = memchr(value, '\0', capacity);
    if (terminator == NULL) {
        return false;
    }
    if (length != NULL) {
        *length = (size_t)(terminator - value);
    }
    return true;
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    value = (char)tolower((unsigned char)value);
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned int bit = 0; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static app_config_status_t normalize_topic(char *topic)
{
    size_t length = 0;
    if (!bounded_string_length(topic, APP_CONFIG_MQTT_TOPIC_MAX_LEN + 1U, &length)) {
        return APP_CONFIG_STATUS_INVALID_STRING;
    }

    size_t first = 0;
    while (first < length && topic[first] == '/') {
        ++first;
    }
    while (length > first && topic[length - 1U] == '/') {
        --length;
    }

    if (first > 0U && length > first) {
        memmove(topic, topic + first, length - first);
    }
    size_t normalized_length = length - first;
    topic[normalized_length] = '\0';

    if (normalized_length == 0U) {
        memcpy(topic, APP_CONFIG_DEFAULT_TOPIC_PREFIX,
               sizeof(APP_CONFIG_DEFAULT_TOPIC_PREFIX));
    }
    return APP_CONFIG_STATUS_OK;
}

void app_config_set_defaults(app_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->schema_version = APP_CONFIG_SCHEMA_VERSION;
    config->mqtt_port = APP_CONFIG_DEFAULT_MQTT_PORT;
    config->mqtt_keepalive = APP_CONFIG_DEFAULT_MQTT_KEEPALIVE;
    config->ble_enabled = true;
    memcpy(config->mqtt_topic_prefix, APP_CONFIG_DEFAULT_TOPIC_PREFIX,
           sizeof(APP_CONFIG_DEFAULT_TOPIC_PREFIX));
}

app_config_status_t app_config_parse_hex(const char *input, size_t input_len,
                                         uint8_t *output, size_t output_len)
{
    if (input == NULL || output == NULL || input_len != output_len * 2U) {
        return APP_CONFIG_STATUS_INVALID_HEX;
    }

    uint8_t parsed[APP_CONFIG_BLE_KEY_LEN];
    if (output_len > sizeof(parsed)) {
        return APP_CONFIG_STATUS_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < output_len; ++i) {
        int high = hex_nibble(input[i * 2U]);
        int low = hex_nibble(input[i * 2U + 1U]);
        if (high < 0 || low < 0) {
            return APP_CONFIG_STATUS_INVALID_HEX;
        }
        parsed[i] = (uint8_t)((high << 4) | low);
    }
    memcpy(output, parsed, output_len);
    return APP_CONFIG_STATUS_OK;
}

app_config_status_t app_config_update_secret(const char *input, size_t input_len,
                                             bool clear, uint8_t *secret,
                                             size_t secret_len, bool *configured)
{
    if (secret == NULL || configured == NULL) {
        return APP_CONFIG_STATUS_INVALID_ARGUMENT;
    }
    if (clear) {
        memset(secret, 0, secret_len);
        *configured = false;
        return APP_CONFIG_STATUS_OK;
    }
    if (input_len == 0U) {
        return APP_CONFIG_STATUS_OK;
    }

    app_config_status_t status = app_config_parse_hex(input, input_len, secret, secret_len);
    if (status == APP_CONFIG_STATUS_OK) {
        *configured = true;
    }
    return status;
}

app_config_status_t app_config_set_mac(app_config_t *config, const char *input,
                                       size_t input_len)
{
    if (config == NULL || input == NULL) {
        return APP_CONFIG_STATUS_INVALID_ARGUMENT;
    }
    if (input_len == 0U) {
        config->charger_mac[0] = '\0';
        return APP_CONFIG_STATUS_OK;
    }
    if (input_len != APP_CONFIG_MAC_TEXT_LEN) {
        return APP_CONFIG_STATUS_INVALID_MAC;
    }

    char normalized[APP_CONFIG_MAC_TEXT_LEN + 1U];
    for (size_t octet = 0; octet < 6U; ++octet) {
        size_t source = octet * 3U;
        int high = hex_nibble(input[source]);
        int low = hex_nibble(input[source + 1U]);
        if (high < 0 || low < 0) {
            return APP_CONFIG_STATUS_INVALID_MAC;
        }
        static const char HEX[] = "0123456789ABCDEF";
        normalized[source] = HEX[high];
        normalized[source + 1U] = HEX[low];
        if (octet < 5U) {
            char separator = input[source + 2U];
            if (separator != ':' && separator != '-') {
                return APP_CONFIG_STATUS_INVALID_MAC;
            }
            normalized[source + 2U] = ':';
        }
    }
    normalized[APP_CONFIG_MAC_TEXT_LEN] = '\0';
    memcpy(config->charger_mac, normalized, sizeof(normalized));
    return APP_CONFIG_STATUS_OK;
}

app_config_status_t app_config_normalize(app_config_t *config)
{
    if (config == NULL) {
        return APP_CONFIG_STATUS_INVALID_ARGUMENT;
    }

    if (!config->wifi_configured) {
        memset(config->wifi_ssid, 0, sizeof(config->wifi_ssid));
        memset(config->wifi_password, 0, sizeof(config->wifi_password));
    }
    if (!config->token_configured) {
        memset(config->token, 0, sizeof(config->token));
    }
    if (!config->ble_key_configured) {
        memset(config->ble_key, 0, sizeof(config->ble_key));
    }
    if (!config->mqtt_password_configured) {
        memset(config->mqtt_password, 0, sizeof(config->mqtt_password));
    }

    char mac[sizeof(config->charger_mac)];
    memcpy(mac, config->charger_mac, sizeof(mac));
    size_t mac_length = 0;
    if (!bounded_string_length(mac, sizeof(mac), &mac_length)) {
        return APP_CONFIG_STATUS_INVALID_STRING;
    }
    app_config_status_t status = app_config_set_mac(config, mac, mac_length);
    if (status != APP_CONFIG_STATUS_OK) {
        return status;
    }

    status = normalize_topic(config->mqtt_topic_prefix);
    if (status != APP_CONFIG_STATUS_OK) {
        return status;
    }
    config->schema_version = APP_CONFIG_SCHEMA_VERSION;
    return app_config_validate(config);
}

app_config_status_t app_config_validate(const app_config_t *config)
{
    if (config == NULL) {
        return APP_CONFIG_STATUS_INVALID_ARGUMENT;
    }
    if (config->schema_version != APP_CONFIG_SCHEMA_VERSION) {
        return APP_CONFIG_STATUS_UNSUPPORTED_SCHEMA;
    }

    size_t ssid_len = 0;
    size_t ignored = 0;
    if (!bounded_string_length(config->wifi_ssid, sizeof(config->wifi_ssid), &ssid_len) ||
        !bounded_string_length(config->wifi_password, sizeof(config->wifi_password), &ignored) ||
        !bounded_string_length(config->charger_mac, sizeof(config->charger_mac), &ignored) ||
        !bounded_string_length(config->mqtt_host, sizeof(config->mqtt_host), &ignored) ||
        !bounded_string_length(config->mqtt_username, sizeof(config->mqtt_username), &ignored) ||
        !bounded_string_length(config->mqtt_password, sizeof(config->mqtt_password), &ignored) ||
        !bounded_string_length(config->mqtt_topic_prefix,
                               sizeof(config->mqtt_topic_prefix), &ignored)) {
        return APP_CONFIG_STATUS_INVALID_STRING;
    }
    if (config->wifi_configured && ssid_len == 0U) {
        return APP_CONFIG_STATUS_INVALID_WIFI;
    }

    size_t mac_len = strlen(config->charger_mac);
    if (mac_len != 0U) {
        app_config_t copy = *config;
        if (app_config_set_mac(&copy, config->charger_mac, mac_len) != APP_CONFIG_STATUS_OK ||
            strcmp(copy.charger_mac, config->charger_mac) != 0) {
            return APP_CONFIG_STATUS_INVALID_MAC;
        }
    }

    if (config->mqtt_host[0] != '\0' && config->mqtt_port == 0U) {
        return APP_CONFIG_STATUS_INVALID_MQTT;
    }
    if (config->mqtt_keepalive == 0U || config->mqtt_topic_prefix[0] == '\0' ||
        strchr(config->mqtt_topic_prefix, '+') != NULL ||
        strchr(config->mqtt_topic_prefix, '#') != NULL ||
        config->mqtt_topic_prefix[0] == '/' ||
        config->mqtt_topic_prefix[strlen(config->mqtt_topic_prefix) - 1U] == '/') {
        return APP_CONFIG_STATUS_INVALID_MQTT;
    }
    return APP_CONFIG_STATUS_OK;
}

void app_config_redact(const app_config_t *config, app_config_public_t *public_config)
{
    if (config == NULL || public_config == NULL) {
        return;
    }
    memset(public_config, 0, sizeof(*public_config));
    public_config->schema_version = config->schema_version;
    public_config->wifi_configured = config->wifi_configured;
    memcpy(public_config->wifi_ssid, config->wifi_ssid, sizeof(public_config->wifi_ssid));
    memcpy(public_config->charger_mac, config->charger_mac,
           sizeof(public_config->charger_mac));
    public_config->token_configured = config->token_configured;
    public_config->ble_key_configured = config->ble_key_configured;
    memcpy(public_config->mqtt_host, config->mqtt_host, sizeof(public_config->mqtt_host));
    public_config->mqtt_port = config->mqtt_port;
    memcpy(public_config->mqtt_username, config->mqtt_username,
           sizeof(public_config->mqtt_username));
    public_config->mqtt_password_configured = config->mqtt_password_configured;
    memcpy(public_config->mqtt_topic_prefix, config->mqtt_topic_prefix,
           sizeof(public_config->mqtt_topic_prefix));
    public_config->mqtt_keepalive = config->mqtt_keepalive;
    public_config->ble_enabled = config->ble_enabled;
}

size_t app_config_blob_max_size(void)
{
    return sizeof(app_config_blob_header_t) + sizeof(app_config_payload_v1_t);
}

static void config_to_v1(const app_config_t *config, app_config_payload_v1_t *payload)
{
    memset(payload, 0, sizeof(*payload));
    payload->wifi_configured = config->wifi_configured;
    memcpy(payload->wifi_ssid, config->wifi_ssid, sizeof(payload->wifi_ssid));
    memcpy(payload->wifi_password, config->wifi_password, sizeof(payload->wifi_password));
    memcpy(payload->charger_mac, config->charger_mac, sizeof(payload->charger_mac));
    memcpy(payload->token, config->token, sizeof(payload->token));
    payload->token_configured = config->token_configured;
    memcpy(payload->ble_key, config->ble_key, sizeof(payload->ble_key));
    payload->ble_key_configured = config->ble_key_configured;
    memcpy(payload->mqtt_host, config->mqtt_host, sizeof(payload->mqtt_host));
    payload->mqtt_port = config->mqtt_port;
    memcpy(payload->mqtt_username, config->mqtt_username, sizeof(payload->mqtt_username));
    memcpy(payload->mqtt_password, config->mqtt_password, sizeof(payload->mqtt_password));
    payload->mqtt_password_configured = config->mqtt_password_configured;
    memcpy(payload->mqtt_topic_prefix, config->mqtt_topic_prefix,
           sizeof(payload->mqtt_topic_prefix));
    payload->mqtt_keepalive = config->mqtt_keepalive;
    payload->ble_enabled = config->ble_enabled;
}

app_config_status_t app_config_encode(const app_config_t *config, uint8_t *blob,
                                      size_t blob_capacity, size_t *blob_len)
{
    if (config == NULL || blob == NULL || blob_len == NULL) {
        return APP_CONFIG_STATUS_INVALID_ARGUMENT;
    }

    app_config_t normalized = *config;
    app_config_status_t status = app_config_normalize(&normalized);
    if (status != APP_CONFIG_STATUS_OK) {
        return status;
    }

    const size_t required = app_config_blob_max_size();
    if (blob_capacity < required) {
        return APP_CONFIG_STATUS_BUFFER_TOO_SMALL;
    }

    app_config_payload_v1_t payload;
    config_to_v1(&normalized, &payload);
    app_config_blob_header_t header = {
        .magic = APP_CONFIG_BLOB_MAGIC,
        .schema_version = APP_CONFIG_SCHEMA_VERSION,
        .payload_size = sizeof(payload),
        .payload_crc32 = crc32((const uint8_t *)&payload, sizeof(payload)),
    };
    memcpy(blob, &header, sizeof(header));
    memcpy(blob + sizeof(header), &payload, sizeof(payload));
    *blob_len = required;
    return APP_CONFIG_STATUS_OK;
}

static app_config_status_t decode_v1(const uint8_t *payload_data, app_config_t *config)
{
    app_config_payload_v1_t payload;
    memcpy(&payload, payload_data, sizeof(payload));
    app_config_set_defaults(config);
    config->wifi_configured = payload.wifi_configured != 0U;
    memcpy(config->wifi_ssid, payload.wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, payload.wifi_password, sizeof(config->wifi_password));
    memcpy(config->charger_mac, payload.charger_mac, sizeof(config->charger_mac));
    memcpy(config->token, payload.token, sizeof(config->token));
    config->token_configured = payload.token_configured != 0U;
    memcpy(config->ble_key, payload.ble_key, sizeof(config->ble_key));
    config->ble_key_configured = payload.ble_key_configured != 0U;
    memcpy(config->mqtt_host, payload.mqtt_host, sizeof(config->mqtt_host));
    config->mqtt_port = payload.mqtt_port;
    memcpy(config->mqtt_username, payload.mqtt_username, sizeof(config->mqtt_username));
    memcpy(config->mqtt_password, payload.mqtt_password, sizeof(config->mqtt_password));
    config->mqtt_password_configured = payload.mqtt_password_configured != 0U;
    memcpy(config->mqtt_topic_prefix, payload.mqtt_topic_prefix,
           sizeof(config->mqtt_topic_prefix));
    config->mqtt_keepalive = payload.mqtt_keepalive;
    config->ble_enabled = payload.ble_enabled != 0U;
    return app_config_normalize(config);
}

static app_config_status_t decode_v0(const uint8_t *payload_data, app_config_t *config)
{
    app_config_payload_v0_t payload;
    memcpy(&payload, payload_data, sizeof(payload));
    app_config_set_defaults(config);
    config->wifi_configured = payload.wifi_configured != 0U;
    memcpy(config->wifi_ssid, payload.wifi_ssid, sizeof(config->wifi_ssid));
    memcpy(config->wifi_password, payload.wifi_password, sizeof(config->wifi_password));
    memcpy(config->mqtt_host, payload.mqtt_host, sizeof(config->mqtt_host));
    config->mqtt_port = payload.mqtt_port;
    memcpy(config->mqtt_username, payload.mqtt_username, sizeof(config->mqtt_username));
    memcpy(config->mqtt_password, payload.mqtt_password, sizeof(config->mqtt_password));
    config->mqtt_password_configured = payload.mqtt_password_configured != 0U;
    return app_config_normalize(config);
}

app_config_status_t app_config_decode(const uint8_t *blob, size_t blob_len,
                                      app_config_t *config, uint16_t *source_schema)
{
    if (blob == NULL || config == NULL || blob_len < sizeof(app_config_blob_header_t)) {
        return APP_CONFIG_STATUS_INVALID_ARGUMENT;
    }

    app_config_blob_header_t header;
    memcpy(&header, blob, sizeof(header));
    if (header.magic != APP_CONFIG_BLOB_MAGIC ||
        blob_len != sizeof(header) + header.payload_size) {
        return APP_CONFIG_STATUS_CORRUPT_BLOB;
    }

    const uint8_t *payload = blob + sizeof(header);
    if (crc32(payload, header.payload_size) != header.payload_crc32) {
        return APP_CONFIG_STATUS_CORRUPT_BLOB;
    }
    if (source_schema != NULL) {
        *source_schema = header.schema_version;
    }
    if (header.schema_version == APP_CONFIG_SCHEMA_VERSION &&
        header.payload_size == sizeof(app_config_payload_v1_t)) {
        return decode_v1(payload, config);
    }
    if (header.schema_version == 0U && header.payload_size == sizeof(app_config_payload_v0_t)) {
        return decode_v0(payload, config);
    }
    return APP_CONFIG_STATUS_UNSUPPORTED_SCHEMA;
}

#ifdef APP_CONFIG_TESTING
app_config_status_t app_config_test_encode_legacy_v0(const app_config_t *config,
                                                     uint8_t *blob,
                                                     size_t blob_capacity,
                                                     size_t *blob_len)
{
    if (config == NULL || blob == NULL || blob_len == NULL) {
        return APP_CONFIG_STATUS_INVALID_ARGUMENT;
    }
    const size_t required = sizeof(app_config_blob_header_t) + sizeof(app_config_payload_v0_t);
    if (blob_capacity < required) {
        return APP_CONFIG_STATUS_BUFFER_TOO_SMALL;
    }

    app_config_payload_v0_t payload = {0};
    payload.wifi_configured = config->wifi_configured;
    memcpy(payload.wifi_ssid, config->wifi_ssid, sizeof(payload.wifi_ssid));
    memcpy(payload.wifi_password, config->wifi_password, sizeof(payload.wifi_password));
    memcpy(payload.mqtt_host, config->mqtt_host, sizeof(payload.mqtt_host));
    payload.mqtt_port = config->mqtt_port;
    memcpy(payload.mqtt_username, config->mqtt_username, sizeof(payload.mqtt_username));
    memcpy(payload.mqtt_password, config->mqtt_password, sizeof(payload.mqtt_password));
    payload.mqtt_password_configured = config->mqtt_password_configured;

    app_config_blob_header_t header = {
        .magic = APP_CONFIG_BLOB_MAGIC,
        .schema_version = 0U,
        .payload_size = sizeof(payload),
        .payload_crc32 = crc32((const uint8_t *)&payload, sizeof(payload)),
    };
    memcpy(blob, &header, sizeof(header));
    memcpy(blob + sizeof(header), &payload, sizeof(payload));
    *blob_len = required;
    return APP_CONFIG_STATUS_OK;
}
#endif
