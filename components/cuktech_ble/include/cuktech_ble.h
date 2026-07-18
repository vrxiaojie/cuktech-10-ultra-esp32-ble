#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config_core.h"
#include "esp_err.h"

#define CUKTECH_BLE_LAST_ERROR_MAX_LEN 63U

typedef enum {
    CUKTECH_BLE_STATE_NOT_STARTED = 0,
    CUKTECH_BLE_STATE_DISABLED,
    CUKTECH_BLE_STATE_WAITING_CONFIG,
    CUKTECH_BLE_STATE_HOST_SYNC,
    CUKTECH_BLE_STATE_SCANNING,
    CUKTECH_BLE_STATE_CONNECTING,
    CUKTECH_BLE_STATE_EXCHANGING_MTU,
    CUKTECH_BLE_STATE_DISCOVERING_SERVICE,
    CUKTECH_BLE_STATE_DISCOVERING_CHARACTERISTICS,
    CUKTECH_BLE_STATE_DISCOVERING_DESCRIPTORS,
    CUKTECH_BLE_STATE_SUBSCRIBING,
    CUKTECH_BLE_STATE_READY,
    CUKTECH_BLE_STATE_AUTHENTICATING,
    CUKTECH_BLE_STATE_AUTHENTICATED,
    CUKTECH_BLE_STATE_AUTH_FAILED_LOCKED,
    CUKTECH_BLE_STATE_DISCONNECTING,
    CUKTECH_BLE_STATE_BACKOFF,
    CUKTECH_BLE_STATE_ERROR,
} cuktech_ble_state_t;

typedef struct {
    cuktech_ble_state_t state;
    bool enabled;
    bool connected;
    bool gatt_ready;
    bool authenticated;
    uint16_t mtu;
    uint32_t notifications_received;
    uint32_t notifications_dropped;
    uint32_t retry_delay_seconds;
    uint32_t authentication_failures;
    char last_error[CUKTECH_BLE_LAST_ERROR_MAX_LEN + 1U];
} cuktech_ble_status_t;

esp_err_t cuktech_ble_start(const app_config_t *config);
void cuktech_ble_get_status(cuktech_ble_status_t *status);
const char *cuktech_ble_state_name(cuktech_ble_state_t state);
