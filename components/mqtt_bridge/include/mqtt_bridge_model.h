#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "charger_state.h"

#define MQTT_BRIDGE_TOPIC_MAX_LEN 159U
#define MQTT_BRIDGE_PORT_JSON_MAX_LEN 160U
#define MQTT_BRIDGE_SETTINGS_JSON_MAX_LEN 512U
#define MQTT_BRIDGE_STATUS_JSON_MAX_LEN 320U
#define MQTT_BRIDGE_PUBLICATION_COUNT 6U
#define MQTT_BRIDGE_PUBLICATION_ALL_MASK ((1U << MQTT_BRIDGE_PUBLICATION_COUNT) - 1U)

typedef enum {
    MQTT_BRIDGE_PUBLICATION_PORT_C1 = 0,
    MQTT_BRIDGE_PUBLICATION_PORT_C2,
    MQTT_BRIDGE_PUBLICATION_PORT_C3,
    MQTT_BRIDGE_PUBLICATION_PORT_A,
    MQTT_BRIDGE_PUBLICATION_SETTINGS,
    MQTT_BRIDGE_PUBLICATION_STATUS,
} mqtt_bridge_publication_t;

extern const char MQTT_BRIDGE_LWT_PAYLOAD[];

bool mqtt_bridge_build_topic(const char *prefix, const char *suffix,
                             char *output, size_t output_size);
bool mqtt_bridge_build_port_json(const cuktech_port_state_t *port,
                                 char *output, size_t output_size);
bool mqtt_bridge_build_settings_json(const charger_state_snapshot_t *state,
                                     char *output, size_t output_size);
bool mqtt_bridge_build_status_json(const charger_state_snapshot_t *state,
                                   char *output, size_t output_size);
bool mqtt_bridge_ports_equal(const cuktech_port_state_t *left,
                             const cuktech_port_state_t *right);
bool mqtt_bridge_settings_equal(const charger_state_snapshot_t *left,
                                const charger_state_snapshot_t *right);
bool mqtt_bridge_status_equal(const charger_state_snapshot_t *left,
                              const charger_state_snapshot_t *right);
int mqtt_bridge_publication_qos(mqtt_bridge_publication_t publication);
bool mqtt_bridge_publication_retain(mqtt_bridge_publication_t publication);
uint32_t mqtt_bridge_publication_mask(
    const charger_state_snapshot_t *current,
    const charger_state_snapshot_t *previous, bool have_previous,
    bool force_full);
