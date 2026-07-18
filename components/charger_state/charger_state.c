#include "charger_state.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mutex;
static charger_state_snapshot_t s_state;

esp_err_t charger_state_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    charger_state_core_reset(&s_state);
    return ESP_OK;
}

void charger_state_set_connection(bool connected, bool authenticated)
{
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_state.connected = connected;
    s_state.authenticated = connected && authenticated;
    ++s_state.revision;
    xSemaphoreGive(s_mutex);
}

void charger_state_set_device_info(const char *device_model,
                                   const char *firmware_version)
{
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (device_model != NULL) {
        snprintf(s_state.device_model, sizeof(s_state.device_model), "%s",
                 device_model);
    }
    if (firmware_version != NULL) {
        snprintf(s_state.firmware_version, sizeof(s_state.firmware_version),
                 "%s", firmware_version);
    }
    ++s_state.revision;
    xSemaphoreGive(s_mutex);
}

bool charger_state_update_setting(uint16_t piid, uint32_t value)
{
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    bool updated = charger_state_core_apply_setting(&s_state, piid, value);
    xSemaphoreGive(s_mutex);
    return updated;
}

bool charger_state_update_port(uint8_t piid,
                               const cuktech_port_state_t *port)
{
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    bool updated = charger_state_core_apply_port(&s_state, piid, port);
    xSemaphoreGive(s_mutex);
    return updated;
}

void charger_state_get_snapshot(charger_state_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        charger_state_core_reset(snapshot);
        return;
    }
    *snapshot = s_state;
    xSemaphoreGive(s_mutex);
}
