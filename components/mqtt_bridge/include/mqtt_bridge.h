#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config_core.h"
#include "esp_err.h"

#define MQTT_BRIDGE_LAST_ERROR_MAX_LEN 63U

typedef enum {
    MQTT_BRIDGE_STATE_NOT_STARTED = 0,
    MQTT_BRIDGE_STATE_WAITING_CONFIG,
    MQTT_BRIDGE_STATE_CONNECTING,
    MQTT_BRIDGE_STATE_CONNECTED,
    MQTT_BRIDGE_STATE_DISCONNECTED,
    MQTT_BRIDGE_STATE_ERROR,
} mqtt_bridge_state_t;

typedef struct {
    mqtt_bridge_state_t state;
    bool configured;
    bool connected;
    uint32_t reconnects;
    uint32_t publish_failures;
    char last_error[MQTT_BRIDGE_LAST_ERROR_MAX_LEN + 1U];
} mqtt_bridge_status_t;

esp_err_t mqtt_bridge_start(const app_config_t *config);
void mqtt_bridge_get_status(mqtt_bridge_status_t *status);
const char *mqtt_bridge_state_name(mqtt_bridge_state_t state);
