/*
 * Protocol estimation logic is derived from state_protocol_v2.py in
 * https://github.com/kairui1108/cuktech-ble-ha at commit
 * 89e5f78387812323528f5967421f65a689e803ef, used under the MIT License.
 */
#include "cuktech_protocol.h"

#include <string.h>

static uint8_t minimum_fixed_voltage_distance(uint8_t voltage_x10)
{
    static const uint8_t FIXED[] = {50U, 90U, 120U, 150U, 200U};
    uint8_t minimum = UINT8_MAX;
    for (size_t i = 0; i < sizeof(FIXED); ++i) {
        uint8_t distance = voltage_x10 > FIXED[i] ? voltage_x10 - FIXED[i]
                                                  : FIXED[i] - voltage_x10;
        if (distance < minimum) {
            minimum = distance;
        }
    }
    return minimum;
}

static cuktech_charge_protocol_t estimate_pd_subtype(uint8_t voltage_x10)
{
    uint8_t distance = minimum_fixed_voltage_distance(voltage_x10);
    if (voltage_x10 < 120U) {
        return distance == 0U ? CUKTECH_CHARGE_PD : CUKTECH_CHARGE_PPS;
    }
    if (distance <= 3U) {
        return CUKTECH_CHARGE_PD;
    }
    if (voltage_x10 >= 30U && voltage_x10 <= 210U) {
        return CUKTECH_CHARGE_PPS;
    }
    return CUKTECH_CHARGE_PD;
}

static cuktech_charge_protocol_t estimate_protocol(
    uint8_t piid, uint8_t code, uint8_t voltage_x10, cuktech_pdo_kind_t pdo_kind,
    const cuktech_type_c_switches_t *switches)
{
    if (piid == 1U || piid == 2U) {
        if (switches != NULL && switches->available && !switches->pd &&
            voltage_x10 > 0U) {
            return CUKTECH_CHARGE_5V;
        }
        if (code == 0x08U) {
            return CUKTECH_CHARGE_PPS;
        }
        if (code == 0x70U) {
            return minimum_fixed_voltage_distance(voltage_x10) == 0U
                       ? CUKTECH_CHARGE_PD
                       : CUKTECH_CHARGE_QC;
        }
        switch (code) {
        case 0x01U:
        case 0x03U:
        case 0x04U:
        case 0x05U:
        case 0x06U:
        case 0x07U:
        case 0x0AU:
        case 0x0BU:
        case 0x30U:
            if (pdo_kind == CUKTECH_PDO_PD_PPS) {
                return minimum_fixed_voltage_distance(voltage_x10) == 0U
                           ? CUKTECH_CHARGE_PD
                           : CUKTECH_CHARGE_PPS;
            }
            if (pdo_kind == CUKTECH_PDO_PD_FIXED) {
                if (switches != NULL && switches->available && switches->pps &&
                    voltage_x10 < 120U) {
                    return estimate_pd_subtype(voltage_x10);
                }
                return CUKTECH_CHARGE_PD;
            }
            return estimate_pd_subtype(voltage_x10);
        default:
            if (minimum_fixed_voltage_distance(voltage_x10) <= 1U) {
                return CUKTECH_CHARGE_PD;
            }
            if (voltage_x10 >= 30U && voltage_x10 <= 210U) {
                return CUKTECH_CHARGE_PPS;
            }
            return CUKTECH_CHARGE_IDLE;
        }
    }
    if (piid == 3U) {
        if (code == 0x70U) {
            return CUKTECH_CHARGE_QC;
        }
        if (voltage_x10 >= 150U) {
            return CUKTECH_CHARGE_PD;
        }
        if (voltage_x10 >= 85U) {
            return CUKTECH_CHARGE_QC;
        }
        if (voltage_x10 <= 55U) {
            return CUKTECH_CHARGE_5V;
        }
        return voltage_x10 > 60U ? CUKTECH_CHARGE_QC : CUKTECH_CHARGE_5V;
    }
    if (piid == 4U) {
        if (code == 0x70U || voltage_x10 > 55U) {
            return CUKTECH_CHARGE_QC;
        }
        if (voltage_x10 > 0U) {
            return CUKTECH_CHARGE_5V;
        }
    }
    return CUKTECH_CHARGE_IDLE;
}

static uint32_t round_power_tenths_even(uint8_t voltage_x10, uint8_t current_x10)
{
    uint32_t product = (uint32_t)voltage_x10 * current_x10;
    uint32_t quotient = product / 10U;
    uint32_t remainder = product % 10U;
    if (remainder > 5U || (remainder == 5U && (quotient & 1U) != 0U)) {
        ++quotient;
    }
    return quotient;
}

cuktech_protocol_status_t cuktech_decode_port(uint8_t piid, const uint8_t *payload,
                                              size_t payload_len,
                                              cuktech_pdo_kind_t pdo_kind,
                                              const cuktech_type_c_switches_t *switches,
                                              cuktech_port_state_t *state)
{
    if (payload == NULL || state == NULL) {
        return CUKTECH_PROTOCOL_INVALID_ARGUMENT;
    }
    if (piid < 1U || piid > 4U || payload_len < 12U) {
        return CUKTECH_PROTOCOL_INVALID_PACKET;
    }
    const uint8_t *raw = payload + payload_len - 4U;
    memset(state, 0, sizeof(*state));
    state->in_use = raw[0] != 0U;
    state->raw_code = raw[1];
    state->current_x10 = raw[2];
    state->voltage_x10 = raw[3];
    state->current = (float)raw[2] / 10.0F;
    state->voltage = (float)raw[3] / 10.0F;
    state->power = (float)round_power_tenths_even(raw[3], raw[2]) / 10.0F;
    state->active = state->in_use || raw[2] > 0U || raw[3] > 0U;
    state->protocol = state->active
                          ? estimate_protocol(piid, raw[1], raw[3], pdo_kind, switches)
                          : CUKTECH_CHARGE_IDLE;
    return CUKTECH_PROTOCOL_OK;
}

const char *cuktech_charge_protocol_name(cuktech_charge_protocol_t protocol)
{
    switch (protocol) {
    case CUKTECH_CHARGE_IDLE:
        return "idle";
    case CUKTECH_CHARGE_5V:
        return "5V";
    case CUKTECH_CHARGE_QC:
        return "QC";
    case CUKTECH_CHARGE_AFC:
        return "AFC";
    case CUKTECH_CHARGE_FCP:
        return "FCP";
    case CUKTECH_CHARGE_SCP:
        return "SCP";
    case CUKTECH_CHARGE_PD:
        return "PD";
    case CUKTECH_CHARGE_PPS:
        return "PPS";
    case CUKTECH_CHARGE_UFCS:
        return "UFCS";
    default:
        return "unknown";
    }
}
