#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cuktech_protocol.h"
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#endif

#define CHARGER_STATE_PORT_COUNT 4U
#define CHARGER_STATE_MAX_PIID 21U
#define CHARGER_STATE_DEVICE_MODEL_MAX_LEN 63U
#define CHARGER_STATE_FIRMWARE_MAX_LEN 31U

typedef struct {
    bool valid;
    uint8_t capability;
    cuktech_pdo_kind_t kind;
} charger_pdo_state_t;

typedef struct {
    bool pd;
    bool pps;
    bool ufcs;
    bool scp;
} charger_protocol_switch_state_t;

typedef struct {
    bool connected;
    bool authenticated;
    cuktech_port_state_t ports[CHARGER_STATE_PORT_COUNT];
    bool setting_valid[CHARGER_STATE_MAX_PIID + 1U];
    uint32_t settings[CHARGER_STATE_MAX_PIID + 1U];
    charger_pdo_state_t pdo[CHARGER_STATE_PORT_COUNT];
    uint32_t protocol_extend;
    charger_protocol_switch_state_t
        protocol_switches[CHARGER_STATE_PORT_COUNT];
    char device_model[CHARGER_STATE_DEVICE_MODEL_MAX_LEN + 1U];
    char firmware_version[CHARGER_STATE_FIRMWARE_MAX_LEN + 1U];
    uint32_t revision;
} charger_state_snapshot_t;

void charger_state_core_reset(charger_state_snapshot_t *state);
bool charger_state_core_apply_setting(charger_state_snapshot_t *state,
                                      uint16_t piid, uint32_t value);
bool charger_state_core_apply_port(charger_state_snapshot_t *state,
                                   uint8_t piid,
                                   const cuktech_port_state_t *port);
void charger_state_core_protocol_inputs(
    const charger_state_snapshot_t *state, uint8_t piid,
    cuktech_pdo_kind_t *pdo_kind, cuktech_type_c_switches_t *switches);

esp_err_t charger_state_init(void);
void charger_state_set_connection(bool connected, bool authenticated);
void charger_state_set_device_info(const char *device_model,
                                   const char *firmware_version);
bool charger_state_update_setting(uint16_t piid, uint32_t value);
bool charger_state_update_port(uint8_t piid,
                               const cuktech_port_state_t *port);
void charger_state_get_snapshot(charger_state_snapshot_t *snapshot);
