#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cuktech_command.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,         \
                    __LINE__, #condition);                                     \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

typedef struct {
    cuktech_command_channel_t channel;
    size_t length;
    uint8_t data[CUKTECH_COMMAND_PACKET_MAX];
} mock_packet_t;

typedef struct {
    mock_packet_t receives[8];
    size_t receive_count;
    size_t receive_index;
    mock_packet_t writes[8];
    size_t write_count;
} mock_transport_t;

static cuktech_command_io_status_t mock_write(
    void *context, cuktech_command_channel_t channel, const uint8_t *data,
    size_t data_len)
{
    mock_transport_t *mock = context;
    CHECK(mock->write_count < 8U);
    mock_packet_t *packet = &mock->writes[mock->write_count++];
    packet->channel = channel;
    packet->length = data_len;
    memcpy(packet->data, data, data_len);
    return CUKTECH_COMMAND_IO_OK;
}

static cuktech_command_io_status_t mock_receive(
    void *context, cuktech_command_channel_t channel, uint8_t *data,
    size_t data_capacity, size_t *data_len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    mock_transport_t *mock = context;
    if (mock->receive_index >= mock->receive_count) {
        return CUKTECH_COMMAND_IO_TIMEOUT;
    }
    mock_packet_t *packet = &mock->receives[mock->receive_index++];
    CHECK(packet->channel == channel);
    CHECK(packet->length <= data_capacity);
    memcpy(data, packet->data, packet->length);
    *data_len = packet->length;
    return CUKTECH_COMMAND_IO_OK;
}

static void queue_packet(mock_transport_t *mock,
                         cuktech_command_channel_t channel,
                         const uint8_t *data, size_t length)
{
    mock_packet_t *packet = &mock->receives[mock->receive_count++];
    packet->channel = channel;
    packet->length = length;
    memcpy(packet->data, data, length);
}

static cuktech_session_t golden_session(void)
{
    const uint8_t dev_key[16] = {
        0xc1, 0x55, 0x87, 0x4e, 0x90, 0x70, 0xce, 0xea,
        0x14, 0x42, 0x26, 0x8a, 0x10, 0x4c, 0x32, 0xa2,
    };
    const uint8_t app_key[16] = {
        0xe3, 0xd4, 0x59, 0x37, 0xfa, 0x34, 0x41, 0x83,
        0x80, 0xb6, 0x74, 0x7d, 0x1c, 0xac, 0x84, 0xe6,
    };
    cuktech_session_t session = {0};
    memcpy(session.dev_key, dev_key, sizeof(dev_key));
    memcpy(session.app_key, app_key, sizeof(app_key));
    memcpy(session.dev_iv, (uint8_t[]){0x8d, 0x9e, 0x08, 0x42}, 4U);
    memcpy(session.app_iv, (uint8_t[]){0xc5, 0x7b, 0x62, 0x32}, 4U);
    session.miot_sequence = 1U;
    return session;
}

static const uint8_t PLAINTEXT[] = {
    0x0c, 0x20, 0x01, 0x00, 0x00, 0x01,
    0x02, 0x05, 0x00, 0x01, 0x10, 0x03,
};
static const uint8_t TX_PACKET[] = {
    0x00, 0x00, 0xc0, 0x63, 0xd9, 0xc3, 0x4f, 0x0a, 0xdb,
    0x76, 0x31, 0x4f, 0x1b, 0xf5, 0x3a, 0xec, 0xd2, 0x55,
};
static const uint8_t RX_PACKET[] = {
    0x00, 0x00, 0x8e, 0x39, 0x99, 0x55, 0x1b, 0x27, 0x6f,
    0x28, 0xac, 0xde, 0xf5, 0xb4, 0xd9, 0x1e, 0x19, 0x71,
};

static cuktech_command_transport_t transport_for(mock_transport_t *mock)
{
    cuktech_command_transport_t transport = {
        .context = mock,
        .write = mock_write,
        .receive = mock_receive,
    };
    return transport;
}

static void test_send(void)
{
    const uint8_t ready[] = {0x00, 0x00, 0x01, 0x01};
    const uint8_t ok[] = {0x00, 0x00, 0x01, 0x00};
    mock_transport_t mock = {0};
    queue_packet(&mock, CUKTECH_COMMAND_CHANNEL_SEND, ready, sizeof(ready));
    queue_packet(&mock, CUKTECH_COMMAND_CHANNEL_SEND, ok, sizeof(ok));
    cuktech_command_transport_t transport = transport_for(&mock);
    cuktech_session_t session = golden_session();
    CHECK(cuktech_command_send(&session, &transport, PLAINTEXT,
                               sizeof(PLAINTEXT)) == CUKTECH_COMMAND_OK);
    CHECK(session.send_counter == 1U);
    CHECK(mock.write_count == 2U);
    CHECK(mock.writes[1].length == sizeof(TX_PACKET) + 2U);
    CHECK(mock.writes[1].data[0] == 1U && mock.writes[1].data[1] == 0U);
    CHECK(memcmp(mock.writes[1].data + 2U, TX_PACKET,
                 sizeof(TX_PACKET)) == 0);

    mock_transport_t exhausted_mock = {0};
    cuktech_command_transport_t exhausted_transport =
        transport_for(&exhausted_mock);
    session = golden_session();
    session.send_counter = CUKTECH_SEND_COUNTER_REKEY;
    CHECK(cuktech_command_send(&session, &exhausted_transport, PLAINTEXT,
                               sizeof(PLAINTEXT)) ==
          CUKTECH_COMMAND_COUNTER_EXHAUSTED);
    CHECK(exhausted_mock.write_count == 0U);
}

static void test_inline_receive(void)
{
    uint8_t inline_packet[sizeof(RX_PACKET) + 4U] = {0x00, 0x00, 0x02, 0x01};
    memcpy(inline_packet + 4U, RX_PACKET, sizeof(RX_PACKET));
    mock_transport_t mock = {0};
    queue_packet(&mock, CUKTECH_COMMAND_CHANNEL_RECEIVE, inline_packet,
                 sizeof(inline_packet));
    cuktech_command_transport_t transport = transport_for(&mock);
    cuktech_session_t session = golden_session();
    uint8_t plaintext[64];
    size_t plaintext_len = 0U;
    CHECK(cuktech_command_receive(&session, &transport, plaintext,
                                  sizeof(plaintext), &plaintext_len,
                                  1000U) == CUKTECH_COMMAND_OK);
    CHECK(plaintext_len == sizeof(PLAINTEXT));
    CHECK(memcmp(plaintext, PLAINTEXT, sizeof(PLAINTEXT)) == 0);
    CHECK(mock.write_count == 1U);
    CHECK(memcmp(mock.writes[0].data,
                 (uint8_t[]){0x00, 0x00, 0x03, 0x00}, 4U) == 0);
}

static void test_multiframe_receive(void)
{
    const uint8_t header[] = {0x00, 0x00, 0x00, 0x01, 0x02, 0x00};
    uint8_t frame1[11] = {0x01, 0x00};
    uint8_t frame2[11] = {0x02, 0x00};
    memcpy(frame1 + 2U, RX_PACKET, 9U);
    memcpy(frame2 + 2U, RX_PACKET + 9U, 9U);
    mock_transport_t mock = {0};
    queue_packet(&mock, CUKTECH_COMMAND_CHANNEL_RECEIVE, header,
                 sizeof(header));
    queue_packet(&mock, CUKTECH_COMMAND_CHANNEL_RECEIVE, frame1,
                 sizeof(frame1));
    queue_packet(&mock, CUKTECH_COMMAND_CHANNEL_RECEIVE, frame2,
                 sizeof(frame2));
    cuktech_command_transport_t transport = transport_for(&mock);
    cuktech_session_t session = golden_session();
    uint8_t plaintext[64];
    size_t plaintext_len = 0U;
    CHECK(cuktech_command_receive(&session, &transport, plaintext,
                                  sizeof(plaintext), &plaintext_len,
                                  1000U) == CUKTECH_COMMAND_OK);
    CHECK(plaintext_len == sizeof(PLAINTEXT));
    CHECK(memcmp(plaintext, PLAINTEXT, sizeof(PLAINTEXT)) == 0);
    CHECK(mock.write_count == 2U);
}

static void test_get_result_parser(void)
{
    const uint8_t response[] = {
        0x11, 0x20, 0x01, 0x00, 0x03, 0x01, 0x02, 0x15, 0x00,
        0x00, 0x00, 0x04, 0x50, 0x0f, 0x0f, 0x03, 0x03,
    };
    uint32_t value = 0U;
    CHECK(cuktech_command_parse_get_result(response, sizeof(response), 2U,
                                            21U, &value));
    CHECK(value == 0x03030f0fU);
    CHECK(!cuktech_command_parse_get_result(response, sizeof(response), 2U,
                                             20U, &value));

    const uint8_t u8_response[] = {
        0x0e, 0x20, 0x02, 0x00, 0x03, 0x01, 0x02,
        0x05, 0x00, 0x00, 0x00, 0x01, 0x10, 0x03,
    };
    CHECK(cuktech_command_parse_get_result(
        u8_response, sizeof(u8_response), 2U, 5U, &value));
    CHECK(value == 3U);
}

int main(void)
{
    test_send();
    test_inline_receive();
    test_multiframe_receive();
    test_get_result_parser();
    puts("command tests passed");
    return 0;
}
