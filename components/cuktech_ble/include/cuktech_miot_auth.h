#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cuktech_protocol.h"

#define CUKTECH_MIOT_AUTH_PACKET_MAX 244U

typedef enum {
    CUKTECH_MIOT_AUTH_CHANNEL_CONTROL = 0,
    CUKTECH_MIOT_AUTH_CHANNEL_DATA,
} cuktech_miot_auth_channel_t;

typedef enum {
    CUKTECH_MIOT_AUTH_IO_OK = 0,
    CUKTECH_MIOT_AUTH_IO_TIMEOUT,
    CUKTECH_MIOT_AUTH_IO_DISCONNECTED,
    CUKTECH_MIOT_AUTH_IO_ERROR,
} cuktech_miot_auth_io_status_t;

typedef struct {
    void *context;
    cuktech_miot_auth_io_status_t (*write)(
        void *context, cuktech_miot_auth_channel_t channel,
        const uint8_t *data, size_t data_len);
    cuktech_miot_auth_io_status_t (*receive)(
        void *context, cuktech_miot_auth_channel_t channel, uint8_t *data,
        size_t data_capacity, size_t *data_len, uint32_t timeout_ms);
    void (*discard)(void *context, cuktech_miot_auth_channel_t channel);
    void (*delay_ms)(void *context, uint32_t delay_ms);
    bool (*fill_random)(void *context, uint8_t *data, size_t data_len);
} cuktech_miot_auth_transport_t;

typedef enum {
    CUKTECH_MIOT_AUTH_OK = 0,
    CUKTECH_MIOT_AUTH_INVALID_ARGUMENT,
    CUKTECH_MIOT_AUTH_INIT_WRITE_FAILED,
    CUKTECH_MIOT_AUTH_INIT_RESPONSE_TIMEOUT,
    CUKTECH_MIOT_AUTH_INIT_RESPONSE_INVALID,
    CUKTECH_MIOT_AUTH_INIT_ACK_FAILED,
    CUKTECH_MIOT_AUTH_KEY_EXCHANGE_TIMEOUT,
    CUKTECH_MIOT_AUTH_KEY_EXCHANGE_INVALID,
    CUKTECH_MIOT_AUTH_PLACEHOLDER_FAILED,
    CUKTECH_MIOT_AUTH_LOGIN_WRITE_FAILED,
    CUKTECH_MIOT_AUTH_RANDOM_GENERATION_FAILED,
    CUKTECH_MIOT_AUTH_RANDOM_HEADER_FAILED,
    CUKTECH_MIOT_AUTH_RANDOM_READY_TIMEOUT,
    CUKTECH_MIOT_AUTH_RANDOM_FRAME_FAILED,
    CUKTECH_MIOT_AUTH_RANDOM_CONFIRM_TIMEOUT,
    CUKTECH_MIOT_AUTH_DEVICE_RANDOM_FAILED,
    CUKTECH_MIOT_AUTH_DEVICE_HMAC_FAILED,
    CUKTECH_MIOT_AUTH_CRYPTO_FAILED,
    CUKTECH_MIOT_AUTH_DEVICE_HMAC_MISMATCH,
    CUKTECH_MIOT_AUTH_HMAC_HEADER_FAILED,
    CUKTECH_MIOT_AUTH_HMAC_READY_TIMEOUT,
    CUKTECH_MIOT_AUTH_HMAC_FRAME_FAILED,
    CUKTECH_MIOT_AUTH_HMAC_CONFIRM_TIMEOUT,
    CUKTECH_MIOT_AUTH_RESULT_TIMEOUT,
    CUKTECH_MIOT_AUTH_RESULT_REJECTED,
    CUKTECH_MIOT_AUTH_RESULT_INVALID,
    CUKTECH_MIOT_AUTH_DISCONNECTED,
    CUKTECH_MIOT_AUTH_TRANSPORT_ERROR,
} cuktech_miot_auth_status_t;

cuktech_miot_auth_status_t cuktech_miot_authenticate(
    const uint8_t token[CUKTECH_TOKEN_SIZE],
    const cuktech_miot_auth_transport_t *transport,
    cuktech_session_t *session);
const char *cuktech_miot_auth_status_name(cuktech_miot_auth_status_t status);
