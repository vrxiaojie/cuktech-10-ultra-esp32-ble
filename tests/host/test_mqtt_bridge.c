#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mqtt_bridge_model.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,         \
                    __LINE__, #condition);                                     \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static void test_topics(void)
{
    char topic[MQTT_BRIDGE_TOPIC_MAX_LEN + 1U];
    CHECK(mqtt_bridge_build_topic("cuktech/charger", "port/c1", topic,
                                  sizeof(topic)));
    CHECK(strcmp(topic, "cuktech/charger/port/c1") == 0);
    CHECK(!mqtt_bridge_build_topic("", "status", topic, sizeof(topic)));
}

static void test_publish_contract(void)
{
    CHECK(strcmp(MQTT_BRIDGE_LWT_PAYLOAD, "{\"connected\":false}") == 0);
    for (mqtt_bridge_publication_t publication =
             MQTT_BRIDGE_PUBLICATION_PORT_C1;
         publication <= MQTT_BRIDGE_PUBLICATION_STATUS; ++publication) {
        CHECK(mqtt_bridge_publication_retain(publication));
    }
    CHECK(mqtt_bridge_publication_qos(MQTT_BRIDGE_PUBLICATION_PORT_C1) == 0);
    CHECK(mqtt_bridge_publication_qos(MQTT_BRIDGE_PUBLICATION_PORT_A) == 0);
    CHECK(mqtt_bridge_publication_qos(MQTT_BRIDGE_PUBLICATION_SETTINGS) == 1);
    CHECK(mqtt_bridge_publication_qos(MQTT_BRIDGE_PUBLICATION_STATUS) == 1);
}

static void test_payloads(void)
{
    cuktech_port_state_t port = {
        .voltage = 20.1F,
        .current = 2.5F,
        .power = 50.2F,
        .active = true,
        .protocol = CUKTECH_CHARGE_PD,
    };
    char port_json[MQTT_BRIDGE_PORT_JSON_MAX_LEN];
    CHECK(mqtt_bridge_build_port_json(&port, port_json, sizeof(port_json)));
    CHECK(strcmp(port_json,
                 "{\"voltage\":20.1,\"current\":2.5,\"power\":50.2,"
                 "\"active\":true,\"protocol\":\"PD\"}") == 0);

    charger_state_snapshot_t state;
    charger_state_core_reset(&state);
    CHECK(charger_state_core_apply_setting(&state, 5U, 3U));
    CHECK(charger_state_core_apply_setting(&state, 16U, 15U));
    CHECK(charger_state_core_apply_setting(&state, 21U, 0x03030f0fU));
    char settings_json[MQTT_BRIDGE_SETTINGS_JSON_MAX_LEN];
    CHECK(mqtt_bridge_build_settings_json(&state, settings_json,
                                           sizeof(settings_json)));
    CHECK(strcmp(settings_json,
                 "{\"5\":3,\"16\":15,\"21\":50532111}") == 0);

    state.connected = true;
    state.authenticated = true;
    snprintf(state.device_model, sizeof(state.device_model), "model\"x");
    snprintf(state.firmware_version, sizeof(state.firmware_version), "1\\2");
    char status_json[MQTT_BRIDGE_STATUS_JSON_MAX_LEN];
    CHECK(mqtt_bridge_build_status_json(&state, status_json,
                                         sizeof(status_json)));
    CHECK(strcmp(status_json,
                 "{\"connected\":true,\"authenticated\":true,"
                 "\"device_model\":\"model\\\"x\","
                 "\"firmware_version\":\"1\\\\2\"}") == 0);
}

static void test_change_detection(void)
{
    charger_state_snapshot_t left;
    charger_state_snapshot_t right;
    charger_state_core_reset(&left);
    charger_state_core_reset(&right);
    CHECK(mqtt_bridge_ports_equal(&left.ports[0], &right.ports[0]));
    CHECK(mqtt_bridge_settings_equal(&left, &right));
    CHECK(mqtt_bridge_status_equal(&left, &right));
    right.ports[0].voltage_x10 = 50U;
    CHECK(!mqtt_bridge_ports_equal(&left.ports[0], &right.ports[0]));
    CHECK(charger_state_core_apply_setting(&right, 5U, 3U));
    CHECK(!mqtt_bridge_settings_equal(&left, &right));
    right.connected = true;
    CHECK(!mqtt_bridge_status_equal(&left, &right));
}

static void test_full_snapshot_and_reconnect(void)
{
    charger_state_snapshot_t previous;
    charger_state_snapshot_t current;
    charger_state_core_reset(&previous);
    charger_state_core_reset(&current);
    CHECK(mqtt_bridge_publication_mask(&current, &previous, false, false) ==
          MQTT_BRIDGE_PUBLICATION_ALL_MASK);
    CHECK(mqtt_bridge_publication_mask(&current, &previous, true, false) ==
          0U);

    current.ports[2].voltage_x10 = 50U;
    CHECK(mqtt_bridge_publication_mask(&current, &previous, true, false) ==
          (1U << MQTT_BRIDGE_PUBLICATION_PORT_C3));

    /* A successful reconnect sets force_full even when a previous snapshot
     * exists, so all retained topics are republished. */
    CHECK(mqtt_bridge_publication_mask(&current, &previous, true, true) ==
          MQTT_BRIDGE_PUBLICATION_ALL_MASK);
}

int main(void)
{
    test_topics();
    test_publish_contract();
    test_payloads();
    test_change_detection();
    test_full_snapshot_and_reconnect();
    puts("mqtt_bridge tests passed");
    return 0;
}
