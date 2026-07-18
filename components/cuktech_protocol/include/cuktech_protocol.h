#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CUKTECH_TOKEN_SIZE 12U
#define CUKTECH_RANDOM_SIZE 16U
#define CUKTECH_KEY_SIZE 16U
#define CUKTECH_IV_SIZE 4U
#define CUKTECH_HMAC_SIZE 32U
#define CUKTECH_CCM_TAG_SIZE 4U
#define CUKTECH_CCM_NONCE_SIZE 12U
#define CUKTECH_SEND_COUNTER_REKEY 0xFFFFU

typedef enum {
    CUKTECH_PROTOCOL_OK = 0,
    CUKTECH_PROTOCOL_INVALID_ARGUMENT,
    CUKTECH_PROTOCOL_BUFFER_TOO_SMALL,
    CUKTECH_PROTOCOL_CRYPTO_ERROR,
    CUKTECH_PROTOCOL_AUTH_FAILED,
    CUKTECH_PROTOCOL_COUNTER_EXHAUSTED,
    CUKTECH_PROTOCOL_INVALID_PACKET,
    CUKTECH_PROTOCOL_INVALID_PIID,
} cuktech_protocol_status_t;

typedef struct {
    uint8_t dev_key[CUKTECH_KEY_SIZE];
    uint8_t app_key[CUKTECH_KEY_SIZE];
    uint8_t dev_iv[CUKTECH_IV_SIZE];
    uint8_t app_iv[CUKTECH_IV_SIZE];
    uint32_t send_counter;
    uint8_t miot_sequence;
} cuktech_session_t;

cuktech_protocol_status_t cuktech_derive_material(
    const uint8_t token[CUKTECH_TOKEN_SIZE],
    const uint8_t app_random[CUKTECH_RANDOM_SIZE],
    const uint8_t dev_random[CUKTECH_RANDOM_SIZE], uint8_t derived[64]);
cuktech_protocol_status_t cuktech_session_derive(
    const uint8_t token[CUKTECH_TOKEN_SIZE],
    const uint8_t app_random[CUKTECH_RANDOM_SIZE],
    const uint8_t dev_random[CUKTECH_RANDOM_SIZE], cuktech_session_t *session,
    uint8_t expected_dev_hmac[CUKTECH_HMAC_SIZE],
    uint8_t app_hmac[CUKTECH_HMAC_SIZE]);
bool cuktech_constant_time_equal(const uint8_t *left, const uint8_t *right,
                                 size_t length);
void cuktech_session_clear(cuktech_session_t *session);

cuktech_protocol_status_t cuktech_encrypt_packet(cuktech_session_t *session,
                                                 const uint8_t *plaintext,
                                                 size_t plaintext_len,
                                                 uint8_t *packet,
                                                 size_t packet_capacity,
                                                 size_t *packet_len);
cuktech_protocol_status_t cuktech_decrypt_packet(const cuktech_session_t *session,
                                                 const uint8_t *packet,
                                                 size_t packet_len,
                                                 uint8_t *plaintext,
                                                 size_t plaintext_capacity,
                                                 size_t *plaintext_len);

cuktech_protocol_status_t cuktech_miot_build_set(uint8_t sequence, uint8_t siid,
                                                 uint16_t piid, uint32_t value,
                                                 uint8_t *output,
                                                 size_t output_capacity,
                                                 size_t *output_len);
cuktech_protocol_status_t cuktech_miot_build_get(uint8_t sequence, uint8_t siid,
                                                 uint16_t piid, uint8_t *output,
                                                 size_t output_capacity,
                                                 size_t *output_len);
bool cuktech_piid_value_valid(uint16_t piid, uint32_t value);

typedef enum {
    CUKTECH_CHARGE_IDLE = 0,
    CUKTECH_CHARGE_5V = 1,
    CUKTECH_CHARGE_QC = 3,
    CUKTECH_CHARGE_AFC = 4,
    CUKTECH_CHARGE_FCP = 5,
    CUKTECH_CHARGE_SCP = 6,
    CUKTECH_CHARGE_PD = 7,
    CUKTECH_CHARGE_PPS = 8,
    CUKTECH_CHARGE_UFCS = 10,
    CUKTECH_CHARGE_UNKNOWN = 255,
} cuktech_charge_protocol_t;

typedef enum {
    CUKTECH_PDO_UNKNOWN = 0,
    CUKTECH_PDO_PD_FIXED,
    CUKTECH_PDO_PD_PPS,
} cuktech_pdo_kind_t;

typedef struct {
    bool available;
    bool pd;
    bool pps;
} cuktech_type_c_switches_t;

typedef struct {
    bool in_use;
    uint8_t raw_code;
    uint8_t current_x10;
    uint8_t voltage_x10;
    float current;
    float voltage;
    float power;
    bool active;
    cuktech_charge_protocol_t protocol;
} cuktech_port_state_t;

cuktech_protocol_status_t cuktech_decode_port(uint8_t piid, const uint8_t *payload,
                                              size_t payload_len,
                                              cuktech_pdo_kind_t pdo_kind,
                                              const cuktech_type_c_switches_t *switches,
                                              cuktech_port_state_t *state);
const char *cuktech_charge_protocol_name(cuktech_charge_protocol_t protocol);
