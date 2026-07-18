#include "app_config_core.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++failures;                                                                     \
        }                                                                                   \
    } while (0)

static bool contains_bytes(const uint8_t *haystack, size_t haystack_len,
                           const uint8_t *needle, size_t needle_len)
{
    if (needle_len == 0U || haystack_len < needle_len) {
        return false;
    }
    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static app_config_t sample_config(void)
{
    app_config_t config;
    app_config_set_defaults(&config);
    config.wifi_configured = true;
    memcpy(config.wifi_ssid, "lab-wifi", sizeof("lab-wifi"));
    memcpy(config.wifi_password, "wifi-secret-9371", sizeof("wifi-secret-9371"));
    CHECK(app_config_set_mac(&config, "aa-bb-cc-dd-ee-ff", 17U) ==
          APP_CONFIG_STATUS_OK);
    CHECK(app_config_update_secret("000102030405060708090a0b", 24U, false,
                                   config.token, sizeof(config.token),
                                   &config.token_configured) == APP_CONFIG_STATUS_OK);
    CHECK(app_config_update_secret("101112131415161718191a1b1c1d1e1f", 32U, false,
                                   config.ble_key, sizeof(config.ble_key),
                                   &config.ble_key_configured) == APP_CONFIG_STATUS_OK);
    memcpy(config.mqtt_host, "mqtt.lan", sizeof("mqtt.lan"));
    config.mqtt_port = 65535U;
    memcpy(config.mqtt_username, "gateway", sizeof("gateway"));
    memcpy(config.mqtt_password, "mqtt-secret-4826", sizeof("mqtt-secret-4826"));
    config.mqtt_password_configured = true;
    memcpy(config.mqtt_topic_prefix, "/cuktech/charger/",
           sizeof("/cuktech/charger/"));
    return config;
}

static void test_defaults_and_empty_configuration(void)
{
    app_config_t config;
    app_config_set_defaults(&config);
    CHECK(app_config_validate(&config) == APP_CONFIG_STATUS_OK);
    CHECK(config.schema_version == APP_CONFIG_SCHEMA_VERSION);
    CHECK(!config.wifi_configured);
    CHECK(!config.token_configured);
    CHECK(!config.ble_key_configured);
    CHECK(config.ble_enabled);
    CHECK(strcmp(config.mqtt_topic_prefix, APP_CONFIG_DEFAULT_TOPIC_PREFIX) == 0);
}

static void test_mac_and_topic_normalization(void)
{
    app_config_t config;
    app_config_set_defaults(&config);
    CHECK(app_config_set_mac(&config, "aa-bB-0c-Dd-ee-Ff", 17U) ==
          APP_CONFIG_STATUS_OK);
    CHECK(strcmp(config.charger_mac, "AA:BB:0C:DD:EE:FF") == 0);
    CHECK(app_config_set_mac(&config, "AA:BB:CC", 8U) ==
          APP_CONFIG_STATUS_INVALID_MAC);

    memcpy(config.mqtt_topic_prefix, "///local/charger///",
           sizeof("///local/charger///"));
    CHECK(app_config_normalize(&config) == APP_CONFIG_STATUS_OK);
    CHECK(strcmp(config.mqtt_topic_prefix, "local/charger") == 0);
    memcpy(config.mqtt_topic_prefix, "bad/+", sizeof("bad/+"));
    CHECK(app_config_normalize(&config) == APP_CONFIG_STATUS_INVALID_MQTT);
}

static void test_hex_and_secret_update_rules(void)
{
    uint8_t token[APP_CONFIG_TOKEN_LEN] = {0};
    bool configured = false;
    CHECK(app_config_update_secret("000102030405060708090a0b", 24U, false, token,
                                   sizeof(token), &configured) == APP_CONFIG_STATUS_OK);
    CHECK(configured);
    uint8_t original[APP_CONFIG_TOKEN_LEN];
    memcpy(original, token, sizeof(original));
    CHECK(app_config_update_secret("", 0U, false, token, sizeof(token), &configured) ==
          APP_CONFIG_STATUS_OK);
    CHECK(memcmp(original, token, sizeof(token)) == 0);
    CHECK(app_config_update_secret("000102", 6U, false, token, sizeof(token),
                                   &configured) == APP_CONFIG_STATUS_INVALID_HEX);
    CHECK(memcmp(original, token, sizeof(token)) == 0);
    CHECK(app_config_update_secret("000102030405060708090a0g", 24U, false, token,
                                   sizeof(token), &configured) == APP_CONFIG_STATUS_INVALID_HEX);
    CHECK(app_config_update_secret(NULL, 0U, true, token, sizeof(token), &configured) ==
          APP_CONFIG_STATUS_OK);
    CHECK(!configured);
    uint8_t zero[APP_CONFIG_TOKEN_LEN] = {0};
    CHECK(memcmp(token, zero, sizeof(token)) == 0);
}

static void test_redaction(void)
{
    app_config_t config = sample_config();
    app_config_public_t public_config;
    app_config_redact(&config, &public_config);
    CHECK(public_config.token_configured);
    CHECK(public_config.ble_key_configured);
    CHECK(public_config.mqtt_password_configured);
    CHECK(strcmp(public_config.charger_mac, "AA:BB:CC:DD:EE:FF") == 0);
    CHECK(!contains_bytes((const uint8_t *)&public_config, sizeof(public_config),
                          (const uint8_t *)config.wifi_password,
                          strlen(config.wifi_password)));
    CHECK(!contains_bytes((const uint8_t *)&public_config, sizeof(public_config),
                          config.token, sizeof(config.token)));
    CHECK(!contains_bytes((const uint8_t *)&public_config, sizeof(public_config),
                          config.ble_key, sizeof(config.ble_key)));
    CHECK(!contains_bytes((const uint8_t *)&public_config, sizeof(public_config),
                          (const uint8_t *)config.mqtt_password,
                          strlen(config.mqtt_password)));
}

static void test_roundtrip_corruption_and_invalid_write(void)
{
    app_config_t config = sample_config();
    uint8_t blob[APP_CONFIG_BLOB_BUFFER_SIZE];
    memset(blob, 0xA5, sizeof(blob));
    size_t blob_len = 0;
    CHECK(app_config_encode(&config, blob, sizeof(blob), &blob_len) == APP_CONFIG_STATUS_OK);
    CHECK(blob_len == app_config_blob_max_size());

    app_config_t decoded;
    uint16_t source_schema = UINT16_MAX;
    CHECK(app_config_decode(blob, blob_len, &decoded, &source_schema) == APP_CONFIG_STATUS_OK);
    CHECK(source_schema == APP_CONFIG_SCHEMA_VERSION);
    CHECK(strcmp(decoded.mqtt_topic_prefix, APP_CONFIG_DEFAULT_TOPIC_PREFIX) == 0);
    CHECK(memcmp(decoded.token, config.token, sizeof(config.token)) == 0);
    CHECK(decoded.mqtt_port == 65535U);

    blob[blob_len - 1U] ^= 0x01U;
    CHECK(app_config_decode(blob, blob_len, &decoded, NULL) ==
          APP_CONFIG_STATUS_CORRUPT_BLOB);

    memset(blob, 0xA5, sizeof(blob));
    size_t unchanged_len = 777U;
    memcpy(config.mqtt_topic_prefix, "bad/#", sizeof("bad/#"));
    CHECK(app_config_encode(&config, blob, sizeof(blob), &unchanged_len) ==
          APP_CONFIG_STATUS_INVALID_MQTT);
    CHECK(unchanged_len == 777U);
    for (size_t i = 0; i < sizeof(blob); ++i) {
        CHECK(blob[i] == 0xA5U);
    }
}

static void test_legacy_migration(void)
{
    app_config_t legacy = sample_config();
    uint8_t blob[APP_CONFIG_BLOB_BUFFER_SIZE] = {0};
    size_t blob_len = 0;
    CHECK(app_config_test_encode_legacy_v0(&legacy, blob, sizeof(blob), &blob_len) ==
          APP_CONFIG_STATUS_OK);

    app_config_t migrated;
    uint16_t source_schema = UINT16_MAX;
    CHECK(app_config_decode(blob, blob_len, &migrated, &source_schema) ==
          APP_CONFIG_STATUS_OK);
    CHECK(source_schema == 0U);
    CHECK(migrated.schema_version == APP_CONFIG_SCHEMA_VERSION);
    CHECK(migrated.wifi_configured);
    CHECK(strcmp(migrated.wifi_ssid, "lab-wifi") == 0);
    CHECK(strcmp(migrated.mqtt_host, "mqtt.lan") == 0);
    CHECK(!migrated.token_configured);
    CHECK(!migrated.ble_key_configured);
    CHECK(migrated.ble_enabled);
    CHECK(strcmp(migrated.mqtt_topic_prefix, APP_CONFIG_DEFAULT_TOPIC_PREFIX) == 0);

    blob[4] = 99U;
    blob[5] = 0U;
    CHECK(app_config_decode(blob, blob_len, &migrated, NULL) ==
          APP_CONFIG_STATUS_UNSUPPORTED_SCHEMA);
}

static void test_unterminated_input_is_rejected(void)
{
    app_config_t config;
    app_config_set_defaults(&config);
    memset(config.mqtt_topic_prefix, 'x', sizeof(config.mqtt_topic_prefix));
    CHECK(app_config_validate(&config) == APP_CONFIG_STATUS_INVALID_STRING);
}

int main(void)
{
    test_defaults_and_empty_configuration();
    test_mac_and_topic_normalization();
    test_hex_and_secret_update_rules();
    test_redaction();
    test_roundtrip_corruption_and_invalid_write();
    test_legacy_migration();
    test_unterminated_input_is_rejected();

    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("app_config host tests passed");
    return 0;
}
