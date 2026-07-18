#include "cuktech_miot_auth.h"

#include <stdio.h>
#include <string.h>

#define MAX_EVENTS 16U
#define MAX_WRITES 16U
#define MAX_PACKET 64U

static int failures;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

typedef struct {
    cuktech_miot_auth_channel_t channel;
    uint8_t data[MAX_PACKET];
    size_t length;
} packet_step_t;

typedef struct {
    packet_step_t events[MAX_EVENTS];
    size_t event_count;
    size_t event_index;
    packet_step_t writes[MAX_WRITES];
    size_t write_count;
    size_t write_index;
    uint8_t random[CUKTECH_RANDOM_SIZE];
    uint32_t total_delay_ms;
    size_t discard_count;
    bool write_mismatch;
} auth_mock_t;

static size_t from_hex(const char *hex, uint8_t *output, size_t output_size)
{
    size_t length = strlen(hex) / 2U;
    if (length > output_size) {
        return 0U;
    }
    for (size_t index = 0U; index < length; ++index) {
        unsigned int value = 0U;
        if (sscanf(hex + index * 2U, "%2x", &value) != 1) {
            return 0U;
        }
        output[index] = (uint8_t)value;
    }
    return length;
}

static void add_hex_step(packet_step_t *steps, size_t *count,
                         cuktech_miot_auth_channel_t channel, const char *hex)
{
    packet_step_t *step = &steps[(*count)++];
    step->channel = channel;
    step->length = from_hex(hex, step->data, sizeof(step->data));
    CHECK(step->length > 0U);
}

static void add_raw_step(packet_step_t *steps, size_t *count,
                         cuktech_miot_auth_channel_t channel,
                         const uint8_t *data, size_t length)
{
    packet_step_t *step = &steps[(*count)++];
    step->channel = channel;
    step->length = length;
    memcpy(step->data, data, length);
}

static cuktech_miot_auth_io_status_t mock_write(
    void *context, cuktech_miot_auth_channel_t channel, const uint8_t *data,
    size_t data_len)
{
    auth_mock_t *mock = context;
    if (mock->write_index >= mock->write_count) {
        mock->write_mismatch = true;
        return CUKTECH_MIOT_AUTH_IO_ERROR;
    }
    const packet_step_t *expected = &mock->writes[mock->write_index++];
    if (expected->channel != channel || expected->length != data_len ||
        memcmp(expected->data, data, data_len) != 0) {
        mock->write_mismatch = true;
        return CUKTECH_MIOT_AUTH_IO_ERROR;
    }
    return CUKTECH_MIOT_AUTH_IO_OK;
}

static cuktech_miot_auth_io_status_t mock_receive(
    void *context, cuktech_miot_auth_channel_t channel, uint8_t *data,
    size_t data_capacity, size_t *data_len, uint32_t timeout_ms)
{
    auth_mock_t *mock = context;
    CHECK(timeout_ms > 0U);
    if (mock->event_index >= mock->event_count) {
        return CUKTECH_MIOT_AUTH_IO_TIMEOUT;
    }
    const packet_step_t *event = &mock->events[mock->event_index];
    if (event->channel != channel) {
        return CUKTECH_MIOT_AUTH_IO_TIMEOUT;
    }
    ++mock->event_index;
    if (event->length > data_capacity) {
        return CUKTECH_MIOT_AUTH_IO_ERROR;
    }
    memcpy(data, event->data, event->length);
    *data_len = event->length;
    return CUKTECH_MIOT_AUTH_IO_OK;
}

static void mock_discard(void *context, cuktech_miot_auth_channel_t channel)
{
    auth_mock_t *mock = context;
    (void)channel;
    ++mock->discard_count;
}

static void mock_delay(void *context, uint32_t delay_ms)
{
    auth_mock_t *mock = context;
    mock->total_delay_ms += delay_ms;
}

static bool mock_random(void *context, uint8_t *data, size_t data_len)
{
    auth_mock_t *mock = context;
    if (data_len != sizeof(mock->random)) {
        return false;
    }
    memcpy(data, mock->random, data_len);
    return true;
}

static void prepare_success_mock(auth_mock_t *mock)
{
    memset(mock, 0, sizeof(*mock));
    CHECK(from_hex("101112131415161718191a1b1c1d1e1f", mock->random,
                   sizeof(mock->random)) == sizeof(mock->random));

    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "0000040006f2");
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "0000040006f2");
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                 "00000401f2f2f2f2f2f2f2f2f2f2f2f2f2f2f2f2");
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "00000101");
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "00000100");
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "0000000b0100");
    uint8_t random_frame[2U + CUKTECH_RANDOM_SIZE] = {0x01, 0x00};
    CHECK(from_hex("202122232425262728292a2b2c2d2e2f", random_frame + 2U,
                   CUKTECH_RANDOM_SIZE) == CUKTECH_RANDOM_SIZE);
    add_raw_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, random_frame,
                 sizeof(random_frame));
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                 "0000020adff4ecc3fa5e7a61d5fb18851afd81655d3733706022e54050d467387f4492f6");
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "00000101");
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "00000100");
    add_hex_step(mock->events, &mock->event_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_CONTROL, "21");

    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_CONTROL, "a4");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "0000050006f2");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                 "00000501f2f2f2f2f2f2f2f2f2f2f2f2f2f2f2f2");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_CONTROL, "24000000");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "0000000b0100");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                 "0100101112131415161718191a1b1c1d1e1f");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "00000101");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "00000100");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "00000300");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA, "0000000a0100");
    add_hex_step(mock->writes, &mock->write_count,
                 CUKTECH_MIOT_AUTH_CHANNEL_DATA,
                 "0100592ed019c8f8146347aa248a1b55ea64ecc0277d9b3a373230817c23c641b649");
}

static cuktech_miot_auth_transport_t make_transport(auth_mock_t *mock)
{
    return (cuktech_miot_auth_transport_t){
        .context = mock,
        .write = mock_write,
        .receive = mock_receive,
        .discard = mock_discard,
        .delay_ms = mock_delay,
        .fill_random = mock_random,
    };
}

static void test_success_with_recovery_and_multiframe(void)
{
    auth_mock_t mock;
    prepare_success_mock(&mock);
    cuktech_miot_auth_transport_t transport = make_transport(&mock);
    uint8_t token[CUKTECH_TOKEN_SIZE];
    CHECK(from_hex("000102030405060708090a0b", token, sizeof(token)) ==
          sizeof(token));
    cuktech_session_t session;
    CHECK(cuktech_miot_authenticate(token, &transport, &session) ==
          CUKTECH_MIOT_AUTH_OK);
    uint8_t expected_dev_key[CUKTECH_KEY_SIZE];
    uint8_t expected_app_key[CUKTECH_KEY_SIZE];
    CHECK(from_hex("c155874e9070ceea1442268a104c32a2", expected_dev_key,
                   sizeof(expected_dev_key)) == sizeof(expected_dev_key));
    CHECK(from_hex("e3d45937fa34418380b6747d1cac84e6", expected_app_key,
                   sizeof(expected_app_key)) == sizeof(expected_app_key));
    CHECK(memcmp(session.dev_key, expected_dev_key, sizeof(expected_dev_key)) == 0);
    CHECK(memcmp(session.app_key, expected_app_key, sizeof(expected_app_key)) == 0);
    CHECK(session.send_counter == 0U && session.miot_sequence == 1U);
    CHECK(mock.event_index == mock.event_count);
    CHECK(mock.write_index == mock.write_count);
    CHECK(!mock.write_mismatch);
    CHECK(mock.total_delay_ms == 650U);
    CHECK(mock.discard_count == 2U);
    cuktech_session_clear(&session);
}

static void test_hmac_mismatch_clears_session(void)
{
    auth_mock_t mock;
    prepare_success_mock(&mock);
    mock.events[7].data[4] ^= 0x01U;
    cuktech_miot_auth_transport_t transport = make_transport(&mock);
    uint8_t token[CUKTECH_TOKEN_SIZE];
    CHECK(from_hex("000102030405060708090a0b", token, sizeof(token)) ==
          sizeof(token));
    cuktech_session_t session;
    memset(&session, 0xa5, sizeof(session));
    CHECK(cuktech_miot_authenticate(token, &transport, &session) ==
          CUKTECH_MIOT_AUTH_DEVICE_HMAC_MISMATCH);
    const uint8_t zero[sizeof(session)] = {0};
    CHECK(memcmp(&session, zero, sizeof(session)) == 0);
    CHECK(strcmp(cuktech_miot_auth_status_name(
                     CUKTECH_MIOT_AUTH_DEVICE_HMAC_MISMATCH),
                 "device_hmac_mismatch") == 0);
}

static void test_rejected_result_clears_session(void)
{
    auth_mock_t mock;
    prepare_success_mock(&mock);
    mock.events[mock.event_count - 1U].data[0] = 0x23U;
    cuktech_miot_auth_transport_t transport = make_transport(&mock);
    uint8_t token[CUKTECH_TOKEN_SIZE];
    CHECK(from_hex("000102030405060708090a0b", token, sizeof(token)) ==
          sizeof(token));
    cuktech_session_t session;
    memset(&session, 0xa5, sizeof(session));
    CHECK(cuktech_miot_authenticate(token, &transport, &session) ==
          CUKTECH_MIOT_AUTH_RESULT_REJECTED);
    const uint8_t zero[sizeof(session)] = {0};
    CHECK(memcmp(&session, zero, sizeof(session)) == 0);
}

static void test_invalid_multiframe_number_is_rejected(void)
{
    auth_mock_t mock;
    prepare_success_mock(&mock);
    mock.events[6].data[0] = 0x00U;
    cuktech_miot_auth_transport_t transport = make_transport(&mock);
    uint8_t token[CUKTECH_TOKEN_SIZE];
    CHECK(from_hex("000102030405060708090a0b", token, sizeof(token)) ==
          sizeof(token));
    cuktech_session_t session;
    CHECK(cuktech_miot_authenticate(token, &transport, &session) ==
          CUKTECH_MIOT_AUTH_DEVICE_RANDOM_FAILED);
}

int main(void)
{
    test_success_with_recovery_and_multiframe();
    test_hmac_mismatch_clears_session();
    test_rejected_result_clears_session();
    test_invalid_multiframe_number_is_rejected();
    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("MiOT auth state machine host tests passed");
    return 0;
}
