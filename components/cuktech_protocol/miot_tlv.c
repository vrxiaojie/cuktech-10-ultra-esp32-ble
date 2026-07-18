#include "cuktech_protocol.h"

static cuktech_protocol_status_t build_tlv(uint8_t sequence, uint8_t siid,
                                           uint16_t piid, uint8_t opcode,
                                           uint8_t type_id, uint32_t value,
                                           size_t value_len, uint8_t *output,
                                           size_t output_capacity,
                                           size_t *output_len)
{
    if (output == NULL || output_len == NULL) {
        return CUKTECH_PROTOCOL_INVALID_ARGUMENT;
    }
    size_t total_len = 11U + value_len;
    if (output_capacity < total_len || total_len > UINT8_MAX) {
        return CUKTECH_PROTOCOL_BUFFER_TOO_SMALL;
    }
    uint16_t type_length = (uint16_t)(((uint16_t)type_id << 12U) | value_len);
    output[0] = (uint8_t)total_len;
    output[1] = 0x20U;
    output[2] = sequence;
    output[3] = 0x00U;
    output[4] = opcode;
    output[5] = 0x01U;
    output[6] = siid;
    output[7] = (uint8_t)piid;
    output[8] = (uint8_t)(piid >> 8U);
    output[9] = (uint8_t)type_length;
    output[10] = (uint8_t)(type_length >> 8U);
    for (size_t i = 0; i < value_len; ++i) {
        output[11U + i] = (uint8_t)(value >> (i * 8U));
    }
    *output_len = total_len;
    return CUKTECH_PROTOCOL_OK;
}

cuktech_protocol_status_t cuktech_miot_build_set(uint8_t sequence, uint8_t siid,
                                                 uint16_t piid, uint32_t value,
                                                 uint8_t *output,
                                                 size_t output_capacity,
                                                 size_t *output_len)
{
    size_t value_len = value <= UINT8_MAX ? 1U : 4U;
    uint8_t type_id = value_len == 1U ? 1U : 5U;
    return build_tlv(sequence, siid, piid, 0x00U, type_id, value, value_len,
                     output, output_capacity, output_len);
}

cuktech_protocol_status_t cuktech_miot_build_get(uint8_t sequence, uint8_t siid,
                                                 uint16_t piid, uint8_t *output,
                                                 size_t output_capacity,
                                                 size_t *output_len)
{
    return build_tlv(sequence, siid, piid, 0x02U, 1U, 0U, 1U, output,
                     output_capacity, output_len);
}

bool cuktech_piid_value_valid(uint16_t piid, uint32_t value)
{
    switch (piid) {
    case 5:
        return value >= 1U && value <= 4U;
    case 6:
        return value <= 5U;
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
        return value <= 1440U;
    case 13:
    case 15:
    case 19:
    case 20:
        return value <= 1U;
    case 16:
        return value <= 15U;
    case 21:
        return true;
    default:
        return false;
    }
}
