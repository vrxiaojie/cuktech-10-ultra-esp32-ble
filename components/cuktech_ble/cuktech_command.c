#include "cuktech_command.h"

#include <string.h>

#define COMMAND_IO_TIMEOUT_MS 3000U
#define COMMAND_MAX_MULTIFRAME_COUNT 64U

static const uint8_t SEND_HEADER[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
static const uint8_t RECEIVE_READY[] = {0x00, 0x00, 0x01, 0x01};
static const uint8_t RECEIVE_OK[] = {0x00, 0x00, 0x01, 0x00};
static const uint8_t INLINE_ACK[] = {0x00, 0x00, 0x03, 0x00};

static cuktech_command_status_t map_io(cuktech_command_io_status_t status)
{
    switch (status) {
    case CUKTECH_COMMAND_IO_OK:
        return CUKTECH_COMMAND_OK;
    case CUKTECH_COMMAND_IO_TIMEOUT:
        return CUKTECH_COMMAND_TIMEOUT;
    case CUKTECH_COMMAND_IO_DISCONNECTED:
        return CUKTECH_COMMAND_DISCONNECTED;
    default:
        return CUKTECH_COMMAND_TRANSPORT_ERROR;
    }
}

static cuktech_command_status_t write_packet(
    const cuktech_command_transport_t *transport,
    cuktech_command_channel_t channel, const uint8_t *data, size_t data_len)
{
    return map_io(transport->write(transport->context, channel, data,
                                   data_len));
}

static cuktech_command_status_t receive_packet(
    const cuktech_command_transport_t *transport,
    cuktech_command_channel_t channel, uint8_t *data, size_t data_capacity,
    size_t *data_len, uint32_t timeout_ms)
{
    return map_io(transport->receive(transport->context, channel, data,
                                     data_capacity, data_len, timeout_ms));
}

static cuktech_command_status_t wait_exact(
    const cuktech_command_transport_t *transport,
    cuktech_command_channel_t channel, const uint8_t *expected,
    size_t expected_len)
{
    uint8_t packet[CUKTECH_COMMAND_PACKET_MAX];
    size_t packet_len = 0U;
    cuktech_command_status_t status = receive_packet(
        transport, channel, packet, sizeof(packet), &packet_len,
        COMMAND_IO_TIMEOUT_MS);
    if (status != CUKTECH_COMMAND_OK) {
        return status;
    }
    return packet_len == expected_len &&
                   memcmp(packet, expected, expected_len) == 0
               ? CUKTECH_COMMAND_OK
               : CUKTECH_COMMAND_PROTOCOL_ERROR;
}

cuktech_command_status_t cuktech_command_send(
    cuktech_session_t *session, const cuktech_command_transport_t *transport,
    const uint8_t *plaintext, size_t plaintext_len)
{
    if (session == NULL || transport == NULL || transport->write == NULL ||
        transport->receive == NULL || plaintext == NULL ||
        plaintext_len == 0U ||
        plaintext_len > CUKTECH_COMMAND_PACKET_MAX - 8U) {
        return CUKTECH_COMMAND_INVALID_ARGUMENT;
    }
    uint8_t encrypted[CUKTECH_COMMAND_PLAINTEXT_MAX + 2U +
                      CUKTECH_CCM_TAG_SIZE];
    size_t encrypted_len = 0U;
    cuktech_protocol_status_t crypto = cuktech_encrypt_packet(
        session, plaintext, plaintext_len, encrypted, sizeof(encrypted),
        &encrypted_len);
    if (crypto == CUKTECH_PROTOCOL_COUNTER_EXHAUSTED) {
        return CUKTECH_COMMAND_COUNTER_EXHAUSTED;
    }
    if (crypto != CUKTECH_PROTOCOL_OK || encrypted_len + 2U > UINT16_MAX) {
        return CUKTECH_COMMAND_CRYPTO_ERROR;
    }

    cuktech_command_status_t status = write_packet(
        transport, CUKTECH_COMMAND_CHANNEL_SEND, SEND_HEADER,
        sizeof(SEND_HEADER));
    if (status != CUKTECH_COMMAND_OK) {
        return status;
    }
    status = wait_exact(transport, CUKTECH_COMMAND_CHANNEL_SEND,
                        RECEIVE_READY, sizeof(RECEIVE_READY));
    if (status != CUKTECH_COMMAND_OK) {
        return status;
    }

    uint8_t frame[sizeof(encrypted) + 2U];
    frame[0] = 0x01U;
    frame[1] = 0x00U;
    memcpy(frame + 2U, encrypted, encrypted_len);
    status = write_packet(transport, CUKTECH_COMMAND_CHANNEL_SEND, frame,
                          encrypted_len + 2U);
    if (status != CUKTECH_COMMAND_OK) {
        return status;
    }
    return wait_exact(transport, CUKTECH_COMMAND_CHANNEL_SEND, RECEIVE_OK,
                      sizeof(RECEIVE_OK));
}

static cuktech_command_status_t decrypt_payload(
    const cuktech_session_t *session, const uint8_t *encrypted,
    size_t encrypted_len, uint8_t *plaintext, size_t plaintext_capacity,
    size_t *plaintext_len)
{
    cuktech_protocol_status_t status = cuktech_decrypt_packet(
        session, encrypted, encrypted_len, plaintext, plaintext_capacity,
        plaintext_len);
    if (status == CUKTECH_PROTOCOL_BUFFER_TOO_SMALL) {
        return CUKTECH_COMMAND_BUFFER_TOO_SMALL;
    }
    return status == CUKTECH_PROTOCOL_OK ? CUKTECH_COMMAND_OK
                                         : CUKTECH_COMMAND_CRYPTO_ERROR;
}

cuktech_command_status_t cuktech_command_receive(
    const cuktech_session_t *session,
    const cuktech_command_transport_t *transport, uint8_t *plaintext,
    size_t plaintext_capacity, size_t *plaintext_len, uint32_t timeout_ms)
{
    if (session == NULL || transport == NULL || transport->write == NULL ||
        transport->receive == NULL || plaintext == NULL ||
        plaintext_len == NULL) {
        return CUKTECH_COMMAND_INVALID_ARGUMENT;
    }
    uint8_t packet[CUKTECH_COMMAND_PACKET_MAX];
    size_t packet_len = 0U;
    cuktech_command_status_t status = receive_packet(
        transport, CUKTECH_COMMAND_CHANNEL_RECEIVE, packet, sizeof(packet),
        &packet_len, timeout_ms);
    if (status != CUKTECH_COMMAND_OK) {
        return status;
    }
    if (packet_len < 4U) {
        return CUKTECH_COMMAND_PROTOCOL_ERROR;
    }

    if (packet[2] == 0x02U) {
        status = write_packet(transport, CUKTECH_COMMAND_CHANNEL_RECEIVE,
                              INLINE_ACK, sizeof(INLINE_ACK));
        if (status != CUKTECH_COMMAND_OK) {
            return status;
        }
        return decrypt_payload(session, packet + 4U, packet_len - 4U,
                               plaintext, plaintext_capacity, plaintext_len);
    }
    if (packet[2] != 0x00U || packet_len < 6U) {
        return CUKTECH_COMMAND_PROTOCOL_ERROR;
    }

    uint16_t frame_count = (uint16_t)packet[4] |
                           ((uint16_t)packet[5] << 8U);
    if (frame_count == 0U || frame_count > COMMAND_MAX_MULTIFRAME_COUNT) {
        return CUKTECH_COMMAND_PROTOCOL_ERROR;
    }
    status = write_packet(transport, CUKTECH_COMMAND_CHANNEL_RECEIVE,
                          RECEIVE_READY, sizeof(RECEIVE_READY));
    if (status != CUKTECH_COMMAND_OK) {
        return status;
    }

    uint8_t encrypted[CUKTECH_COMMAND_PLAINTEXT_MAX + 2U +
                      CUKTECH_CCM_TAG_SIZE];
    size_t encrypted_len = 0U;
    for (uint16_t index = 0U; index < frame_count; ++index) {
        packet_len = 0U;
        status = receive_packet(transport, CUKTECH_COMMAND_CHANNEL_RECEIVE,
                                packet, sizeof(packet), &packet_len,
                                COMMAND_IO_TIMEOUT_MS);
        if (status != CUKTECH_COMMAND_OK) {
            (void)write_packet(transport, CUKTECH_COMMAND_CHANNEL_RECEIVE,
                               RECEIVE_OK, sizeof(RECEIVE_OK));
            return status;
        }
        uint16_t received_index = 0U;
        if (packet_len >= 2U) {
            received_index = (uint16_t)((uint16_t)packet[0] |
                                        ((uint16_t)packet[1] << 8U));
        }
        size_t fragment_len = packet_len >= 2U ? packet_len - 2U : 0U;
        if (received_index != index + 1U ||
            fragment_len > sizeof(encrypted) - encrypted_len) {
            (void)write_packet(transport, CUKTECH_COMMAND_CHANNEL_RECEIVE,
                               RECEIVE_OK, sizeof(RECEIVE_OK));
            return CUKTECH_COMMAND_PROTOCOL_ERROR;
        }
        memcpy(encrypted + encrypted_len, packet + 2U, fragment_len);
        encrypted_len += fragment_len;
    }
    status = write_packet(transport, CUKTECH_COMMAND_CHANNEL_RECEIVE,
                          RECEIVE_OK, sizeof(RECEIVE_OK));
    if (status != CUKTECH_COMMAND_OK) {
        return status;
    }
    return decrypt_payload(session, encrypted, encrypted_len, plaintext,
                           plaintext_capacity, plaintext_len);
}

bool cuktech_command_parse_get_result(const uint8_t *plaintext,
                                      size_t plaintext_len, uint8_t siid,
                                      uint16_t piid, uint32_t *value)
{
    if (plaintext == NULL || value == NULL || plaintext_len < 14U ||
        plaintext[1] != 0x20U || plaintext[4] != 0x03U ||
        plaintext[6] != siid || plaintext[7] != (uint8_t)piid ||
        plaintext[8] != (uint8_t)(piid >> 8U)) {
        return false;
    }
    uint8_t value_len = plaintext[11];
    if (value_len >= 4U) {
        if (plaintext_len < 17U) {
            return false;
        }
        *value = (uint32_t)plaintext[13] |
                 ((uint32_t)plaintext[14] << 8U) |
                 ((uint32_t)plaintext[15] << 16U) |
                 ((uint32_t)plaintext[16] << 24U);
    } else {
        *value = plaintext[13];
    }
    return true;
}

const char *cuktech_command_status_name(cuktech_command_status_t status)
{
    switch (status) {
    case CUKTECH_COMMAND_OK:
        return "ok";
    case CUKTECH_COMMAND_TIMEOUT:
        return "timeout";
    case CUKTECH_COMMAND_DISCONNECTED:
        return "disconnected";
    case CUKTECH_COMMAND_TRANSPORT_ERROR:
        return "transport_error";
    case CUKTECH_COMMAND_PROTOCOL_ERROR:
        return "protocol_error";
    case CUKTECH_COMMAND_CRYPTO_ERROR:
        return "crypto_error";
    case CUKTECH_COMMAND_COUNTER_EXHAUSTED:
        return "counter_exhausted";
    case CUKTECH_COMMAND_BUFFER_TOO_SMALL:
        return "buffer_too_small";
    default:
        return "invalid_argument";
    }
}
