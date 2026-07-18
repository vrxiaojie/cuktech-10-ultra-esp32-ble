#include "cuktech_miot_auth.h"

#include <string.h>

#define AUTH_SHORT_TIMEOUT_MS 3000U
#define AUTH_KEY_EXCHANGE_TIMEOUT_MS 5000U
#define AUTH_RESULT_TIMEOUT_MS 5000U
#define AUTH_READY_ATTEMPTS 5U
#define AUTH_DELAY_AFTER_PLACEHOLDER_MS 600U
#define AUTH_DELAY_BEFORE_LOGIN_MS 50U
#define AUTH_MAX_MULTIFRAME_COUNT 64U

static const uint8_t RECEIVE_READY[] = {0x00, 0x00, 0x01, 0x01};
static const uint8_t RECEIVE_OK[] = {0x00, 0x00, 0x01, 0x00};
static const uint8_t INLINE_ACK[] = {0x00, 0x00, 0x03, 0x00};

static void secure_zero(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (length-- > 0U) {
        *bytes++ = 0U;
    }
}

static cuktech_miot_auth_status_t map_io_status(
    cuktech_miot_auth_io_status_t status,
    cuktech_miot_auth_status_t timeout_status,
    cuktech_miot_auth_status_t error_status)
{
    switch (status) {
    case CUKTECH_MIOT_AUTH_IO_OK:
        return CUKTECH_MIOT_AUTH_OK;
    case CUKTECH_MIOT_AUTH_IO_TIMEOUT:
        return timeout_status;
    case CUKTECH_MIOT_AUTH_IO_DISCONNECTED:
        return CUKTECH_MIOT_AUTH_DISCONNECTED;
    default:
        return error_status;
    }
}

static cuktech_miot_auth_status_t write_auth(
    const cuktech_miot_auth_transport_t *transport,
    cuktech_miot_auth_channel_t channel, const uint8_t *data, size_t data_len,
    cuktech_miot_auth_status_t failure_status)
{
    return map_io_status(
        transport->write(transport->context, channel, data, data_len),
        failure_status, failure_status);
}

static cuktech_miot_auth_status_t receive_auth(
    const cuktech_miot_auth_transport_t *transport,
    cuktech_miot_auth_channel_t channel, uint8_t *data, size_t data_capacity,
    size_t *data_len, uint32_t timeout_ms,
    cuktech_miot_auth_status_t timeout_status,
    cuktech_miot_auth_status_t error_status)
{
    return map_io_status(
        transport->receive(transport->context, channel, data, data_capacity,
                           data_len, timeout_ms),
        timeout_status, error_status);
}

static cuktech_miot_auth_status_t wait_for_exact(
    const cuktech_miot_auth_transport_t *transport,
    cuktech_miot_auth_channel_t channel, const uint8_t *expected,
    size_t expected_len, size_t attempts,
    cuktech_miot_auth_status_t timeout_status)
{
    uint8_t packet[CUKTECH_MIOT_AUTH_PACKET_MAX];
    for (size_t attempt = 0U; attempt < attempts; ++attempt) {
        size_t packet_len = 0U;
        cuktech_miot_auth_status_t status = receive_auth(
            transport, channel, packet, sizeof(packet), &packet_len,
            AUTH_SHORT_TIMEOUT_MS, timeout_status,
            CUKTECH_MIOT_AUTH_TRANSPORT_ERROR);
        if (status != CUKTECH_MIOT_AUTH_OK) {
            secure_zero(packet, sizeof(packet));
            return status;
        }
        bool matches = packet_len == expected_len &&
                       memcmp(packet, expected, expected_len) == 0;
        secure_zero(packet, sizeof(packet));
        if (matches) {
            return CUKTECH_MIOT_AUTH_OK;
        }
    }
    return timeout_status;
}

static cuktech_miot_auth_status_t receive_auth_response(
    const cuktech_miot_auth_transport_t *transport, uint8_t *output,
    size_t output_capacity, size_t *output_len,
    cuktech_miot_auth_status_t failure_status)
{
    uint8_t packet[CUKTECH_MIOT_AUTH_PACKET_MAX];
    size_t packet_len = 0U;
    cuktech_miot_auth_status_t status = receive_auth(
        transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, packet, sizeof(packet),
        &packet_len, AUTH_SHORT_TIMEOUT_MS, failure_status, failure_status);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        secure_zero(packet, sizeof(packet));
        return status;
    }
    if (packet_len < 4U) {
        secure_zero(packet, sizeof(packet));
        return failure_status;
    }

    if (packet[2] == 0x02U) {
        size_t payload_len = packet_len - 4U;
        if (payload_len > output_capacity) {
            secure_zero(packet, sizeof(packet));
            return failure_status;
        }
        memcpy(output, packet + 4U, payload_len);
        *output_len = payload_len;
        secure_zero(packet, sizeof(packet));
        return write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                          INLINE_ACK, sizeof(INLINE_ACK), failure_status);
    }

    if (packet[2] != 0x00U || packet_len < 6U) {
        secure_zero(packet, sizeof(packet));
        return failure_status;
    }
    uint16_t frame_count = (uint16_t)packet[4] | ((uint16_t)packet[5] << 8U);
    secure_zero(packet, sizeof(packet));
    if (frame_count == 0U || frame_count > AUTH_MAX_MULTIFRAME_COUNT) {
        return failure_status;
    }
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                        RECEIVE_READY, sizeof(RECEIVE_READY), failure_status);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        return status;
    }

    size_t total = 0U;
    for (uint16_t frame_index = 0U; frame_index < frame_count; ++frame_index) {
        packet_len = 0U;
        status = receive_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                              packet, sizeof(packet), &packet_len,
                              AUTH_SHORT_TIMEOUT_MS, failure_status,
                              failure_status);
        if (status != CUKTECH_MIOT_AUTH_OK) {
            secure_zero(packet, sizeof(packet));
            return status;
        }
        if (packet_len < 2U || packet_len - 2U > output_capacity - total) {
            secure_zero(packet, sizeof(packet));
            return failure_status;
        }
        uint16_t received_index =
            (uint16_t)packet[0] | ((uint16_t)packet[1] << 8U);
        if (received_index != frame_index + 1U) {
            secure_zero(packet, sizeof(packet));
            return failure_status;
        }
        memcpy(output + total, packet + 2U, packet_len - 2U);
        total += packet_len - 2U;
        secure_zero(packet, sizeof(packet));
    }
    *output_len = total;
    return write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, RECEIVE_OK,
                      sizeof(RECEIVE_OK), failure_status);
}

cuktech_miot_auth_status_t cuktech_miot_authenticate(
    const uint8_t token[CUKTECH_TOKEN_SIZE],
    const cuktech_miot_auth_transport_t *transport,
    cuktech_session_t *session)
{
    if (token == NULL || transport == NULL || session == NULL ||
        transport->write == NULL || transport->receive == NULL ||
        transport->discard == NULL || transport->delay_ms == NULL ||
        transport->fill_random == NULL) {
        return CUKTECH_MIOT_AUTH_INVALID_ARGUMENT;
    }
    cuktech_session_clear(session);
    uint8_t packet[CUKTECH_MIOT_AUTH_PACKET_MAX];
    uint8_t app_random[CUKTECH_RANDOM_SIZE] = {0};
    uint8_t dev_random[CUKTECH_RANDOM_SIZE] = {0};
    uint8_t dev_hmac[CUKTECH_HMAC_SIZE] = {0};
    uint8_t expected_dev_hmac[CUKTECH_HMAC_SIZE] = {0};
    uint8_t app_hmac[CUKTECH_HMAC_SIZE] = {0};
    cuktech_miot_auth_status_t status = CUKTECH_MIOT_AUTH_OK;

    transport->discard(transport->context, CUKTECH_MIOT_AUTH_CHANNEL_DATA);
    const uint8_t initialize[] = {0xa4};
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_CONTROL,
                        initialize, sizeof(initialize),
                        CUKTECH_MIOT_AUTH_INIT_WRITE_FAILED);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }

    size_t packet_len = 0U;
    status = receive_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, packet,
                          sizeof(packet), &packet_len,
                          AUTH_SHORT_TIMEOUT_MS,
                          CUKTECH_MIOT_AUTH_INIT_RESPONSE_TIMEOUT,
                          CUKTECH_MIOT_AUTH_TRANSPORT_ERROR);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    if (packet_len < 3U || packet[2] == 0xffU) {
        status = CUKTECH_MIOT_AUTH_INIT_RESPONSE_INVALID;
        goto cleanup;
    }
    ++packet[2];
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, packet,
                        packet_len, CUKTECH_MIOT_AUTH_INIT_ACK_FAILED);
    secure_zero(packet, sizeof(packet));
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }

    packet_len = 0U;
    status = receive_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, packet,
                          sizeof(packet), &packet_len,
                          AUTH_KEY_EXCHANGE_TIMEOUT_MS,
                          CUKTECH_MIOT_AUTH_KEY_EXCHANGE_TIMEOUT,
                          CUKTECH_MIOT_AUTH_TRANSPORT_ERROR);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    if (packet_len >= 3U && packet_len < 20U && packet[2] == 0x04U) {
        bool recovered = false;
        for (size_t attempt = 0U; attempt < 3U; ++attempt) {
            secure_zero(packet, sizeof(packet));
            packet_len = 0U;
            status = receive_auth(
                transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, packet,
                sizeof(packet), &packet_len, AUTH_SHORT_TIMEOUT_MS,
                CUKTECH_MIOT_AUTH_KEY_EXCHANGE_TIMEOUT,
                CUKTECH_MIOT_AUTH_TRANSPORT_ERROR);
            if (status != CUKTECH_MIOT_AUTH_OK) {
                goto cleanup;
            }
            if (packet_len >= 20U && packet[2] == 0x04U) {
                recovered = true;
                break;
            }
        }
        if (!recovered) {
            status = CUKTECH_MIOT_AUTH_KEY_EXCHANGE_INVALID;
            goto cleanup;
        }
    }
    if (packet_len < 4U) {
        status = CUKTECH_MIOT_AUTH_KEY_EXCHANGE_INVALID;
        goto cleanup;
    }
    size_t placeholder_len = packet_len;
    memset(packet, 0xf2, placeholder_len);
    packet[0] = 0x00;
    packet[1] = 0x00;
    packet[2] = 0x05;
    packet[3] = 0x01;
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, packet,
                        placeholder_len, CUKTECH_MIOT_AUTH_PLACEHOLDER_FAILED);
    secure_zero(packet, sizeof(packet));
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    transport->delay_ms(transport->context, AUTH_DELAY_AFTER_PLACEHOLDER_MS);
    transport->discard(transport->context, CUKTECH_MIOT_AUTH_CHANNEL_DATA);
    transport->delay_ms(transport->context, AUTH_DELAY_BEFORE_LOGIN_MS);

    const uint8_t login[] = {0x24, 0x00, 0x00, 0x00};
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_CONTROL, login,
                        sizeof(login), CUKTECH_MIOT_AUTH_LOGIN_WRITE_FAILED);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    if (!transport->fill_random(transport->context, app_random,
                                sizeof(app_random))) {
        status = CUKTECH_MIOT_AUTH_RANDOM_GENERATION_FAILED;
        goto cleanup;
    }

    const uint8_t random_header[] = {0x00, 0x00, 0x00, 0x0b, 0x01, 0x00};
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                        random_header, sizeof(random_header),
                        CUKTECH_MIOT_AUTH_RANDOM_HEADER_FAILED);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    status = wait_for_exact(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                            RECEIVE_READY, sizeof(RECEIVE_READY),
                            AUTH_READY_ATTEMPTS,
                            CUKTECH_MIOT_AUTH_RANDOM_READY_TIMEOUT);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    packet[0] = 0x01;
    packet[1] = 0x00;
    memcpy(packet + 2U, app_random, sizeof(app_random));
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, packet,
                        sizeof(app_random) + 2U,
                        CUKTECH_MIOT_AUTH_RANDOM_FRAME_FAILED);
    secure_zero(packet, sizeof(packet));
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    status = wait_for_exact(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                            RECEIVE_OK, sizeof(RECEIVE_OK), 1U,
                            CUKTECH_MIOT_AUTH_RANDOM_CONFIRM_TIMEOUT);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }

    size_t response_len = 0U;
    status = receive_auth_response(transport, dev_random, sizeof(dev_random),
                                   &response_len,
                                   CUKTECH_MIOT_AUTH_DEVICE_RANDOM_FAILED);
    if (status != CUKTECH_MIOT_AUTH_OK || response_len < sizeof(dev_random)) {
        status = status == CUKTECH_MIOT_AUTH_OK
                     ? CUKTECH_MIOT_AUTH_DEVICE_RANDOM_FAILED
                     : status;
        goto cleanup;
    }
    response_len = 0U;
    status = receive_auth_response(transport, dev_hmac, sizeof(dev_hmac),
                                   &response_len,
                                   CUKTECH_MIOT_AUTH_DEVICE_HMAC_FAILED);
    if (status != CUKTECH_MIOT_AUTH_OK || response_len < sizeof(dev_hmac)) {
        status = status == CUKTECH_MIOT_AUTH_OK
                     ? CUKTECH_MIOT_AUTH_DEVICE_HMAC_FAILED
                     : status;
        goto cleanup;
    }

    if (cuktech_session_derive(token, app_random, dev_random, session,
                               expected_dev_hmac, app_hmac) !=
        CUKTECH_PROTOCOL_OK) {
        status = CUKTECH_MIOT_AUTH_CRYPTO_FAILED;
        goto cleanup;
    }
    if (!cuktech_constant_time_equal(expected_dev_hmac, dev_hmac,
                                     sizeof(dev_hmac))) {
        status = CUKTECH_MIOT_AUTH_DEVICE_HMAC_MISMATCH;
        goto cleanup;
    }

    const uint8_t hmac_header[] = {0x00, 0x00, 0x00, 0x0a, 0x01, 0x00};
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                        hmac_header, sizeof(hmac_header),
                        CUKTECH_MIOT_AUTH_HMAC_HEADER_FAILED);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    status = wait_for_exact(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                            RECEIVE_READY, sizeof(RECEIVE_READY), 1U,
                            CUKTECH_MIOT_AUTH_HMAC_READY_TIMEOUT);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    packet[0] = 0x01;
    packet[1] = 0x00;
    memcpy(packet + 2U, app_hmac, sizeof(app_hmac));
    status = write_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA, packet,
                        sizeof(app_hmac) + 2U,
                        CUKTECH_MIOT_AUTH_HMAC_FRAME_FAILED);
    secure_zero(packet, sizeof(packet));
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    status = wait_for_exact(transport, CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                            RECEIVE_OK, sizeof(RECEIVE_OK), 1U,
                            CUKTECH_MIOT_AUTH_HMAC_CONFIRM_TIMEOUT);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }

    packet_len = 0U;
    status = receive_auth(transport, CUKTECH_MIOT_AUTH_CHANNEL_CONTROL,
                          packet, sizeof(packet), &packet_len,
                          AUTH_RESULT_TIMEOUT_MS,
                          CUKTECH_MIOT_AUTH_RESULT_TIMEOUT,
                          CUKTECH_MIOT_AUTH_TRANSPORT_ERROR);
    if (status != CUKTECH_MIOT_AUTH_OK) {
        goto cleanup;
    }
    if (packet_len == 0U) {
        status = CUKTECH_MIOT_AUTH_RESULT_INVALID;
        goto cleanup;
    }
    if (packet[0] == 0x21U || packet[0] == 0x11U) {
        status = CUKTECH_MIOT_AUTH_OK;
    } else if (packet[0] == 0x23U || packet[0] == 0x12U) {
        status = CUKTECH_MIOT_AUTH_RESULT_REJECTED;
    } else {
        status = CUKTECH_MIOT_AUTH_RESULT_INVALID;
    }

cleanup:
    secure_zero(packet, sizeof(packet));
    secure_zero(app_random, sizeof(app_random));
    secure_zero(dev_random, sizeof(dev_random));
    secure_zero(dev_hmac, sizeof(dev_hmac));
    secure_zero(expected_dev_hmac, sizeof(expected_dev_hmac));
    secure_zero(app_hmac, sizeof(app_hmac));
    if (status != CUKTECH_MIOT_AUTH_OK) {
        cuktech_session_clear(session);
    }
    return status;
}

const char *cuktech_miot_auth_status_name(cuktech_miot_auth_status_t status)
{
    static const char *const names[] = {
        [CUKTECH_MIOT_AUTH_OK] = "ok",
        [CUKTECH_MIOT_AUTH_INVALID_ARGUMENT] = "invalid_argument",
        [CUKTECH_MIOT_AUTH_INIT_WRITE_FAILED] = "init_write_failed",
        [CUKTECH_MIOT_AUTH_INIT_RESPONSE_TIMEOUT] = "init_response_timeout",
        [CUKTECH_MIOT_AUTH_INIT_RESPONSE_INVALID] = "init_response_invalid",
        [CUKTECH_MIOT_AUTH_INIT_ACK_FAILED] = "init_ack_failed",
        [CUKTECH_MIOT_AUTH_KEY_EXCHANGE_TIMEOUT] = "key_exchange_timeout",
        [CUKTECH_MIOT_AUTH_KEY_EXCHANGE_INVALID] = "key_exchange_invalid",
        [CUKTECH_MIOT_AUTH_PLACEHOLDER_FAILED] = "placeholder_failed",
        [CUKTECH_MIOT_AUTH_LOGIN_WRITE_FAILED] = "login_write_failed",
        [CUKTECH_MIOT_AUTH_RANDOM_GENERATION_FAILED] = "random_generation_failed",
        [CUKTECH_MIOT_AUTH_RANDOM_HEADER_FAILED] = "random_header_failed",
        [CUKTECH_MIOT_AUTH_RANDOM_READY_TIMEOUT] = "random_ready_timeout",
        [CUKTECH_MIOT_AUTH_RANDOM_FRAME_FAILED] = "random_frame_failed",
        [CUKTECH_MIOT_AUTH_RANDOM_CONFIRM_TIMEOUT] = "random_confirm_timeout",
        [CUKTECH_MIOT_AUTH_DEVICE_RANDOM_FAILED] = "device_random_failed",
        [CUKTECH_MIOT_AUTH_DEVICE_HMAC_FAILED] = "device_hmac_failed",
        [CUKTECH_MIOT_AUTH_CRYPTO_FAILED] = "crypto_failed",
        [CUKTECH_MIOT_AUTH_DEVICE_HMAC_MISMATCH] = "device_hmac_mismatch",
        [CUKTECH_MIOT_AUTH_HMAC_HEADER_FAILED] = "hmac_header_failed",
        [CUKTECH_MIOT_AUTH_HMAC_READY_TIMEOUT] = "hmac_ready_timeout",
        [CUKTECH_MIOT_AUTH_HMAC_FRAME_FAILED] = "hmac_frame_failed",
        [CUKTECH_MIOT_AUTH_HMAC_CONFIRM_TIMEOUT] = "hmac_confirm_timeout",
        [CUKTECH_MIOT_AUTH_RESULT_TIMEOUT] = "result_timeout",
        [CUKTECH_MIOT_AUTH_RESULT_REJECTED] = "result_rejected",
        [CUKTECH_MIOT_AUTH_RESULT_INVALID] = "result_invalid",
        [CUKTECH_MIOT_AUTH_DISCONNECTED] = "disconnected",
        [CUKTECH_MIOT_AUTH_TRANSPORT_ERROR] = "transport_error",
    };
    return status >= 0 && (size_t)status < sizeof(names) / sizeof(names[0]) &&
                   names[status] != NULL
               ? names[status]
               : "unknown";
}
