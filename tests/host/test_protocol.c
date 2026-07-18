#include "cuktech_protocol.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++failures;                                                                     \
        }                                                                                   \
    } while (0)

static size_t from_hex(const char *hex, uint8_t *output, size_t output_size)
{
    size_t length = strlen(hex) / 2U;
    if (length > output_size) {
        return 0U;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned int value = 0;
        if (sscanf(hex + i * 2U, "%2x", &value) != 1) {
            return 0U;
        }
        output[i] = (uint8_t)value;
    }
    return length;
}

static void test_crypto_golden_vectors(void)
{
    uint8_t token[12], app_random[16], dev_random[16], expected_derived[64];
    uint8_t expected_dev_hmac[32], expected_app_hmac[32], plaintext[32];
    uint8_t expected_tx[64], expected_rx[64];
    CHECK(from_hex("000102030405060708090a0b", token, sizeof(token)) == sizeof(token));
    CHECK(from_hex("101112131415161718191a1b1c1d1e1f", app_random,
                   sizeof(app_random)) == sizeof(app_random));
    CHECK(from_hex("202122232425262728292a2b2c2d2e2f", dev_random,
                   sizeof(dev_random)) == sizeof(dev_random));
    CHECK(from_hex("c155874e9070ceea1442268a104c32a2e3d45937fa34418380b6747d1cac84e6"
                   "8d9e0842c57b62327150bbb07ed0a674489c7512056817bf62079e1b97056604",
                   expected_derived, sizeof(expected_derived)) == sizeof(expected_derived));
    CHECK(from_hex("dff4ecc3fa5e7a61d5fb18851afd81655d3733706022e54050d467387f4492f6",
                   expected_dev_hmac, sizeof(expected_dev_hmac)) ==
          sizeof(expected_dev_hmac));
    CHECK(from_hex("592ed019c8f8146347aa248a1b55ea64ecc0277d9b3a373230817c23c641b649",
                   expected_app_hmac, sizeof(expected_app_hmac)) ==
          sizeof(expected_app_hmac));
    size_t plaintext_len =
        from_hex("0c2001000001020500011003", plaintext, sizeof(plaintext));
    size_t tx_len = from_hex("0000c063d9c34f0adb76314f1bf53aecd255", expected_tx,
                             sizeof(expected_tx));
    size_t rx_len = from_hex("00008e3999551b276f28acdef5b4d91e1971", expected_rx,
                             sizeof(expected_rx));

    uint8_t derived[64];
    CHECK(cuktech_derive_material(token, app_random, dev_random, derived) ==
          CUKTECH_PROTOCOL_OK);
    CHECK(memcmp(derived, expected_derived, sizeof(derived)) == 0);

    cuktech_session_t session;
    uint8_t dev_hmac[32], app_hmac[32];
    CHECK(cuktech_session_derive(token, app_random, dev_random, &session, dev_hmac,
                                 app_hmac) == CUKTECH_PROTOCOL_OK);
    CHECK(memcmp(session.dev_key, expected_derived, 16U) == 0);
    CHECK(memcmp(session.app_key, expected_derived + 16U, 16U) == 0);
    CHECK(memcmp(session.dev_iv, expected_derived + 32U, 4U) == 0);
    CHECK(memcmp(session.app_iv, expected_derived + 36U, 4U) == 0);
    CHECK(memcmp(dev_hmac, expected_dev_hmac, sizeof(dev_hmac)) == 0);
    CHECK(memcmp(app_hmac, expected_app_hmac, sizeof(app_hmac)) == 0);
    CHECK(cuktech_constant_time_equal(dev_hmac, expected_dev_hmac, sizeof(dev_hmac)));

    uint8_t packet[64];
    size_t packet_len = 0;
    CHECK(cuktech_encrypt_packet(&session, plaintext, plaintext_len, packet,
                                 sizeof(packet), &packet_len) == CUKTECH_PROTOCOL_OK);
    CHECK(packet_len == tx_len);
    CHECK(memcmp(packet, expected_tx, tx_len) == 0);
    CHECK(session.send_counter == 1U);

    uint8_t decrypted[64];
    size_t decrypted_len = 0;
    CHECK(cuktech_decrypt_packet(&session, expected_rx, rx_len, decrypted,
                                 sizeof(decrypted), &decrypted_len) == CUKTECH_PROTOCOL_OK);
    CHECK(decrypted_len == plaintext_len);
    CHECK(memcmp(decrypted, plaintext, plaintext_len) == 0);
    expected_rx[rx_len - 1U] ^= 1U;
    CHECK(cuktech_decrypt_packet(&session, expected_rx, rx_len, decrypted,
                                 sizeof(decrypted), &decrypted_len) ==
          CUKTECH_PROTOCOL_AUTH_FAILED);

    session.send_counter = CUKTECH_SEND_COUNTER_REKEY;
    CHECK(cuktech_encrypt_packet(&session, plaintext, plaintext_len, packet,
                                 sizeof(packet), &packet_len) ==
          CUKTECH_PROTOCOL_COUNTER_EXHAUSTED);
    cuktech_session_clear(&session);
}

static void test_miot_vectors_and_ranges(void)
{
    uint8_t output[32], expected[32];
    size_t output_len = 0;
    size_t expected_len = from_hex("0c2001000001020500011003", expected,
                                   sizeof(expected));
    CHECK(cuktech_miot_build_set(1U, 2U, 5U, 3U, output, sizeof(output),
                                 &output_len) == CUKTECH_PROTOCOL_OK);
    CHECK(output_len == expected_len && memcmp(output, expected, output_len) == 0);

    expected_len = from_hex("0c2001000201020500011000", expected, sizeof(expected));
    CHECK(cuktech_miot_build_get(1U, 2U, 5U, output, sizeof(output), &output_len) ==
          CUKTECH_PROTOCOL_OK);
    CHECK(output_len == expected_len && memcmp(output, expected, output_len) == 0);

    expected_len = from_hex("0f200100000102150004500f0f0303", expected,
                            sizeof(expected));
    CHECK(cuktech_miot_build_set(1U, 2U, 21U, 0x03030F0FU, output,
                                 sizeof(output), &output_len) == CUKTECH_PROTOCOL_OK);
    CHECK(output_len == expected_len && memcmp(output, expected, output_len) == 0);

    CHECK(cuktech_piid_value_valid(5U, 1U));
    CHECK(cuktech_piid_value_valid(5U, 4U));
    CHECK(!cuktech_piid_value_valid(5U, 0U));
    CHECK(cuktech_piid_value_valid(8U, 1440U));
    CHECK(!cuktech_piid_value_valid(8U, 1441U));
    CHECK(cuktech_piid_value_valid(21U, UINT32_MAX));
    CHECK(!cuktech_piid_value_valid(17U, 0U));
}

static void make_payload(uint8_t *payload, uint8_t in_use, uint8_t code,
                         uint8_t current_x10, uint8_t voltage_x10)
{
    memset(payload, 0, 12U);
    payload[8] = in_use;
    payload[9] = code;
    payload[10] = current_x10;
    payload[11] = voltage_x10;
}

static void check_protocol_case(uint8_t piid, uint8_t code, uint8_t current_x10,
                                uint8_t voltage_x10,
                                cuktech_charge_protocol_t expected)
{
    uint8_t payload[12];
    make_payload(payload, voltage_x10 > 0U, code, current_x10, voltage_x10);
    cuktech_port_state_t state;
    CHECK(cuktech_decode_port(piid, payload, sizeof(payload), CUKTECH_PDO_UNKNOWN,
                              NULL, &state) == CUKTECH_PROTOCOL_OK);
    CHECK(state.protocol == expected);
}

static void test_port_decode_and_detection(void)
{
    uint8_t payload[12];
    make_payload(payload, 1U, 0x0AU, 25U, 201U);
    cuktech_port_state_t state;
    CHECK(cuktech_decode_port(1U, payload, sizeof(payload), CUKTECH_PDO_UNKNOWN,
                              NULL, &state) == CUKTECH_PROTOCOL_OK);
    CHECK(state.voltage == 20.1F);
    CHECK(state.current == 2.5F);
    CHECK(state.power == 50.2F);
    CHECK(state.active);
    CHECK(state.protocol == CUKTECH_CHARGE_PD);
    CHECK(strcmp(cuktech_charge_protocol_name(state.protocol), "PD") == 0);

    check_protocol_case(1U, 0x07U, 20U, 50U, CUKTECH_CHARGE_PD);
    check_protocol_case(1U, 0x0AU, 12U, 92U, CUKTECH_CHARGE_PPS);
    check_protocol_case(2U, 0x03U, 15U, 201U, CUKTECH_CHARGE_PD);
    check_protocol_case(3U, 0x60U, 3U, 121U, CUKTECH_CHARGE_QC);
    check_protocol_case(3U, 0x80U, 10U, 200U, CUKTECH_CHARGE_PD);
    check_protocol_case(4U, 0x60U, 10U, 50U, CUKTECH_CHARGE_5V);
    check_protocol_case(4U, 0x60U, 10U, 90U, CUKTECH_CHARGE_QC);

    make_payload(payload, 0U, 0U, 0U, 0U);
    CHECK(cuktech_decode_port(1U, payload, sizeof(payload), CUKTECH_PDO_UNKNOWN,
                              NULL, &state) == CUKTECH_PROTOCOL_OK);
    CHECK(!state.active && state.protocol == CUKTECH_CHARGE_IDLE);
    CHECK(cuktech_decode_port(1U, payload, 4U, CUKTECH_PDO_UNKNOWN, NULL, &state) ==
          CUKTECH_PROTOCOL_INVALID_PACKET);
}

int main(void)
{
    test_crypto_golden_vectors();
    test_miot_vectors_and_ranges();
    test_port_decode_and_detection();
    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("cuktech protocol host tests passed");
    return 0;
}
