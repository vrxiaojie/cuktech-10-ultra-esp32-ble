#include "charger_state.h"

#include <string.h>

static charger_pdo_state_t decode_pdo_half(uint16_t half)
{
    charger_pdo_state_t result = {
        .valid = (half & 0xffU) != 0U,
        .capability = (uint8_t)(half & 0xffU),
        .kind = CUKTECH_PDO_UNKNOWN,
    };
    if (!result.valid) {
        return result;
    }
    switch ((uint8_t)(half >> 8U)) {
    case 0x07U:
        result.kind = CUKTECH_PDO_PD_FIXED;
        break;
    case 0x08U:
        result.kind = CUKTECH_PDO_PD_PPS;
        break;
    default:
        break;
    }
    return result;
}

static void decode_protocol_extend(charger_state_snapshot_t *state,
                                   uint32_t value)
{
    memset(state->protocol_switches, 0,
           sizeof(state->protocol_switches));
    state->protocol_switches[0].pd = (value & (1UL << 0U)) != 0U;
    state->protocol_switches[0].pps = (value & (1UL << 1U)) != 0U;
    state->protocol_switches[0].ufcs = (value & (1UL << 2U)) != 0U;
    state->protocol_switches[1].pd = (value & (1UL << 8U)) != 0U;
    state->protocol_switches[1].pps = (value & (1UL << 9U)) != 0U;
    state->protocol_switches[1].ufcs = (value & (1UL << 10U)) != 0U;
    state->protocol_switches[2].ufcs = (value & (1UL << 16U)) != 0U;
    state->protocol_switches[2].scp = (value & (1UL << 17U)) != 0U;
    state->protocol_switches[3].ufcs = (value & (1UL << 24U)) != 0U;
    state->protocol_switches[3].scp = (value & (1UL << 25U)) != 0U;
}

void charger_state_core_reset(charger_state_snapshot_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    for (size_t index = 0U; index < CHARGER_STATE_PORT_COUNT; ++index) {
        state->ports[index].protocol = CUKTECH_CHARGE_IDLE;
        state->pdo[index].kind = CUKTECH_PDO_UNKNOWN;
    }
}

bool charger_state_core_apply_setting(charger_state_snapshot_t *state,
                                      uint16_t piid, uint32_t value)
{
    if (state == NULL || piid > CHARGER_STATE_MAX_PIID || piid < 5U ||
        piid == 7U || piid == 14U) {
        return false;
    }
    state->settings[piid] = value;
    state->setting_valid[piid] = true;
    if (piid == 17U) {
        state->pdo[1] = decode_pdo_half((uint16_t)value);
        state->pdo[0] = decode_pdo_half((uint16_t)(value >> 16U));
    } else if (piid == 18U) {
        state->pdo[3] = decode_pdo_half((uint16_t)value);
        state->pdo[2] = decode_pdo_half((uint16_t)(value >> 16U));
    } else if (piid == 21U) {
        state->protocol_extend = value;
        decode_protocol_extend(state, value);
    }
    ++state->revision;
    return true;
}

bool charger_state_core_apply_port(charger_state_snapshot_t *state,
                                   uint8_t piid,
                                   const cuktech_port_state_t *port)
{
    if (state == NULL || port == NULL || piid < 1U || piid > 4U) {
        return false;
    }
    state->ports[piid - 1U] = *port;
    ++state->revision;
    return true;
}

void charger_state_core_protocol_inputs(
    const charger_state_snapshot_t *state, uint8_t piid,
    cuktech_pdo_kind_t *pdo_kind, cuktech_type_c_switches_t *switches)
{
    if (pdo_kind != NULL) {
        *pdo_kind = CUKTECH_PDO_UNKNOWN;
    }
    if (switches != NULL) {
        memset(switches, 0, sizeof(*switches));
    }
    if (state == NULL || piid < 1U || piid > 4U) {
        return;
    }
    size_t index = piid - 1U;
    if (pdo_kind != NULL && state->pdo[index].valid) {
        *pdo_kind = state->pdo[index].kind;
    }
    if (switches != NULL && piid <= 2U) {
        switches->available = state->setting_valid[21U];
        switches->pd = state->protocol_switches[index].pd;
        switches->pps = state->protocol_switches[index].pps;
    }
}

