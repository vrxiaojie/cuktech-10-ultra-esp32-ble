#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cuktech_protocol.h"

#define CUKTECH_COMMAND_PACKET_MAX 244U
#define CUKTECH_COMMAND_PLAINTEXT_MAX 512U

typedef enum {
    CUKTECH_COMMAND_CHANNEL_SEND = 0,
    CUKTECH_COMMAND_CHANNEL_RECEIVE,
} cuktech_command_channel_t;

typedef enum {
    CUKTECH_COMMAND_IO_OK = 0,
    CUKTECH_COMMAND_IO_TIMEOUT,
    CUKTECH_COMMAND_IO_DISCONNECTED,
    CUKTECH_COMMAND_IO_ERROR,
} cuktech_command_io_status_t;

typedef struct {
    void *context;
    cuktech_command_io_status_t (*write)(void *context,
                                         cuktech_command_channel_t channel,
                                         const uint8_t *data,
                                         size_t data_len);
    cuktech_command_io_status_t (*receive)(
        void *context, cuktech_command_channel_t channel, uint8_t *data,
        size_t data_capacity, size_t *data_len, uint32_t timeout_ms);
} cuktech_command_transport_t;

typedef enum {
    CUKTECH_COMMAND_OK = 0,
    CUKTECH_COMMAND_TIMEOUT,
    CUKTECH_COMMAND_DISCONNECTED,
    CUKTECH_COMMAND_TRANSPORT_ERROR,
    CUKTECH_COMMAND_PROTOCOL_ERROR,
    CUKTECH_COMMAND_CRYPTO_ERROR,
    CUKTECH_COMMAND_COUNTER_EXHAUSTED,
    CUKTECH_COMMAND_BUFFER_TOO_SMALL,
    CUKTECH_COMMAND_INVALID_ARGUMENT,
} cuktech_command_status_t;

cuktech_command_status_t cuktech_command_send(
    cuktech_session_t *session, const cuktech_command_transport_t *transport,
    const uint8_t *plaintext, size_t plaintext_len);
cuktech_command_status_t cuktech_command_receive(
    const cuktech_session_t *session,
    const cuktech_command_transport_t *transport, uint8_t *plaintext,
    size_t plaintext_capacity, size_t *plaintext_len, uint32_t timeout_ms);
bool cuktech_command_parse_get_result(const uint8_t *plaintext,
                                      size_t plaintext_len, uint8_t siid,
                                      uint16_t piid, uint32_t *value);
const char *cuktech_command_status_name(cuktech_command_status_t status);

