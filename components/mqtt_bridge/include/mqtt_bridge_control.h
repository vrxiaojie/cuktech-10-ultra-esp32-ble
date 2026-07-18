#pragma once

#include <stddef.h>

#include "cuktech_control.h"

typedef enum {
    MQTT_BRIDGE_CONTROL_OK = 0,
    MQTT_BRIDGE_CONTROL_INVALID_ARGUMENT,
    MQTT_BRIDGE_CONTROL_INVALID_JSON,
    MQTT_BRIDGE_CONTROL_MISSING_FIELD,
    MQTT_BRIDGE_CONTROL_INVALID_FIELD,
    MQTT_BRIDGE_CONTROL_OUT_OF_RANGE,
} mqtt_bridge_control_status_t;

mqtt_bridge_control_status_t mqtt_bridge_parse_set_command(
    const char *payload, size_t payload_len,
    cuktech_control_command_t *command);
mqtt_bridge_control_status_t mqtt_bridge_parse_port_command(
    const char *payload, size_t payload_len,
    cuktech_control_command_t *command);
const char *mqtt_bridge_control_status_name(
    mqtt_bridge_control_status_t status);
