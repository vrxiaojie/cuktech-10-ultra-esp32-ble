#include "web_config_model.h"

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

static app_config_t configured(void)
{
    app_config_t config;
    app_config_set_defaults(&config);
    config.wifi_configured = true;
    memcpy(config.wifi_ssid, "trusted-lan", sizeof("trusted-lan"));
    memcpy(config.wifi_password, "wifi-do-not-return", sizeof("wifi-do-not-return"));
    memcpy(config.mqtt_password, "mqtt-do-not-return", sizeof("mqtt-do-not-return"));
    config.mqtt_password_configured = true;
    CHECK(app_config_update_secret("000102030405060708090a0b", 24U, false,
                                   config.token, sizeof(config.token),
                                   &config.token_configured) == APP_CONFIG_STATUS_OK);
    return config;
}

static void test_valid_partial_update_and_secret_redaction(void)
{
    app_config_t config = configured();
    const char json[] =
        "{\"charger_mac\":\"aa-bb-cc-dd-ee-ff\","
        "\"token\":\"101112131415161718191a1b\","
        "\"ble_key\":\"202122232425262728292a2b2c2d2e2f\","
        "\"mqtt_host\":\"broker.lan\",\"mqtt_port\":1884,"
        "\"mqtt_username\":\"gateway\",\"mqtt_password\":\"new-mqtt-secret\","
        "\"mqtt_topic_prefix\":\"/cuktech/charger/\",\"mqtt_keepalive\":90,"
        "\"ble_enabled\":true}";
    CHECK(web_config_apply_json(json, strlen(json), &config) == WEB_CONFIG_MODEL_OK);
    CHECK(strcmp(config.charger_mac, "AA:BB:CC:DD:EE:FF") == 0);
    CHECK(strcmp(config.mqtt_host, "broker.lan") == 0);
    CHECK(config.mqtt_port == 1884U);
    CHECK(strcmp(config.mqtt_topic_prefix, "cuktech/charger") == 0);
    CHECK(config.token_configured && config.ble_key_configured);

    char output[WEB_CONFIG_PUBLIC_JSON_SIZE];
    CHECK(web_config_build_public_json(&config, output, sizeof(output)) ==
          WEB_CONFIG_MODEL_OK);
    CHECK(strstr(output, "token_configured") != NULL);
    CHECK(strstr(output, "ble_key_configured") != NULL);
    CHECK(strstr(output, "mqtt_password_configured") != NULL);
    CHECK(strstr(output, "wifi-do-not-return") == NULL);
    CHECK(strstr(output, "new-mqtt-secret") == NULL);
    CHECK(strstr(output, "101112131415161718191a1b") == NULL);
    CHECK(strstr(output, "202122232425262728292a2b2c2d2e2f") == NULL);
}

static void test_empty_secret_keeps_and_clear_is_explicit(void)
{
    app_config_t config = configured();
    uint8_t old_token[APP_CONFIG_TOKEN_LEN];
    memcpy(old_token, config.token, sizeof(old_token));
    const char keep[] = "{\"token\":\"\",\"mqtt_password\":\"\"}";
    CHECK(web_config_apply_json(keep, strlen(keep), &config) == WEB_CONFIG_MODEL_OK);
    CHECK(memcmp(config.token, old_token, sizeof(old_token)) == 0);
    CHECK(strcmp(config.mqtt_password, "mqtt-do-not-return") == 0);

    const char clear[] = "{\"clear_token\":true,\"clear_mqtt_password\":true}";
    CHECK(web_config_apply_json(clear, strlen(clear), &config) == WEB_CONFIG_MODEL_OK);
    CHECK(!config.token_configured);
    CHECK(!config.mqtt_password_configured);
    uint8_t zero[APP_CONFIG_TOKEN_LEN] = {0};
    CHECK(memcmp(config.token, zero, sizeof(zero)) == 0);
    CHECK(config.mqtt_password[0] == '\0');
}

static void check_rejected_without_mutation(const char *json,
                                            web_config_model_status_t expected)
{
    app_config_t config = configured();
    app_config_t before = config;
    CHECK(web_config_apply_json(json, strlen(json), &config) == expected);
    CHECK(memcmp(&config, &before, sizeof(config)) == 0);
}

static void test_invalid_requests(void)
{
    check_rejected_without_mutation("not-json", WEB_CONFIG_MODEL_INVALID_JSON);
    check_rejected_without_mutation("{}", WEB_CONFIG_MODEL_MISSING_FIELDS);
    check_rejected_without_mutation("{\"mqtt_port\":0}",
                                    WEB_CONFIG_MODEL_INVALID_FIELD);
    check_rejected_without_mutation("{\"mqtt_port\":1.5}",
                                    WEB_CONFIG_MODEL_INVALID_FIELD);
    check_rejected_without_mutation("{\"token\":\"0011\"}",
                                    WEB_CONFIG_MODEL_INVALID_FIELD);
    check_rejected_without_mutation("{\"ble_enabled\":\"yes\"}",
                                    WEB_CONFIG_MODEL_INVALID_FIELD);
    check_rejected_without_mutation("{\"mqtt_topic_prefix\":\"bad/#\"}",
                                    WEB_CONFIG_MODEL_INVALID_CONFIG);

    char oversized[WEB_CONFIG_API_MAX_BODY_SIZE + 1U];
    memset(oversized, 'x', sizeof(oversized));
    app_config_t config = configured();
    CHECK(web_config_apply_json(oversized, sizeof(oversized), &config) ==
          WEB_CONFIG_MODEL_BODY_TOO_LARGE);
}

int main(void)
{
    test_valid_partial_update_and_secret_redaction();
    test_empty_secret_keeps_and_clear_is_explicit();
    test_invalid_requests();
    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("web_config host tests passed");
    return 0;
}
