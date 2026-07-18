#pragma once

#include <stddef.h>

#include "app_config_core.h"
#include "esp_err.h"

typedef enum {
    WIFI_MANAGER_STATE_STOPPED = 0,
    WIFI_MANAGER_STATE_PROVISIONING_AP,
    WIFI_MANAGER_STATE_STA_CONNECTING,
    WIFI_MANAGER_STATE_STA_CONNECTED,
    WIFI_MANAGER_STATE_VERIFYING_STA,
    WIFI_MANAGER_STATE_FALLBACK_AP,
} wifi_manager_state_t;

typedef enum {
    WIFI_MANAGER_PROVISION_OK = 0,
    WIFI_MANAGER_PROVISION_INVALID_INPUT,
    WIFI_MANAGER_PROVISION_BUSY,
    WIFI_MANAGER_PROVISION_CONNECT_FAILED,
    WIFI_MANAGER_PROVISION_SAVE_FAILED,
    WIFI_MANAGER_PROVISION_INTERNAL_ERROR,
} wifi_manager_provision_result_t;

esp_err_t wifi_manager_start(const app_config_t *config);
wifi_manager_state_t wifi_manager_get_state(void);
const char *wifi_manager_state_name(wifi_manager_state_t state);
const char *wifi_manager_get_ap_ssid(void);
wifi_manager_provision_result_t wifi_manager_provision(const char *ssid,
                                                       const char *password,
                                                       char *ip_address,
                                                       size_t ip_address_size);
