#include "cuktech_protocol.h"

#include <string.h>

#include "crypto_backend.h"

static const uint8_t LOGIN_INFO[] = "mible-login-info";

static void write_le32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

bool cuktech_constant_time_equal(const uint8_t *left, const uint8_t *right,
                                 size_t length)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0U;
}

cuktech_protocol_status_t cuktech_session_derive(
    const uint8_t token[CUKTECH_TOKEN_SIZE],
    const uint8_t app_random[CUKTECH_RANDOM_SIZE],
    const uint8_t dev_random[CUKTECH_RANDOM_SIZE], cuktech_session_t *session,
    uint8_t expected_dev_hmac[CUKTECH_HMAC_SIZE],
    uint8_t app_hmac[CUKTECH_HMAC_SIZE])
{
    if (token == NULL || app_random == NULL || dev_random == NULL || session == NULL ||
        expected_dev_hmac == NULL || app_hmac == NULL) {
        return CUKTECH_PROTOCOL_INVALID_ARGUMENT;
    }

    uint8_t salt_inverse[CUKTECH_RANDOM_SIZE * 2U];
    uint8_t derived[64];
    memcpy(salt_inverse, dev_random, CUKTECH_RANDOM_SIZE);
    memcpy(salt_inverse + CUKTECH_RANDOM_SIZE, app_random, CUKTECH_RANDOM_SIZE);

    cuktech_protocol_status_t derive_status =
        cuktech_derive_material(token, app_random, dev_random, derived);
    if (derive_status != CUKTECH_PROTOCOL_OK) {
        cuktech_crypto_zeroize(salt_inverse, sizeof(salt_inverse));
        cuktech_crypto_zeroize(derived, sizeof(derived));
        return derive_status;
    }

    memset(session, 0, sizeof(*session));
    memcpy(session->dev_key, derived, CUKTECH_KEY_SIZE);
    memcpy(session->app_key, derived + 16U, CUKTECH_KEY_SIZE);
    memcpy(session->dev_iv, derived + 32U, CUKTECH_IV_SIZE);
    memcpy(session->app_iv, derived + 36U, CUKTECH_IV_SIZE);
    session->miot_sequence = 1U;

    int result = cuktech_crypto_hmac_sha256(session->dev_key, CUKTECH_KEY_SIZE,
                                            salt_inverse, sizeof(salt_inverse),
                                            expected_dev_hmac);
    if (result == 0) {
        uint8_t salt[CUKTECH_RANDOM_SIZE * 2U];
        memcpy(salt, app_random, CUKTECH_RANDOM_SIZE);
        memcpy(salt + CUKTECH_RANDOM_SIZE, dev_random, CUKTECH_RANDOM_SIZE);
        result = cuktech_crypto_hmac_sha256(session->app_key, CUKTECH_KEY_SIZE,
                                            salt, sizeof(salt), app_hmac);
        cuktech_crypto_zeroize(salt, sizeof(salt));
    }
    cuktech_crypto_zeroize(salt_inverse, sizeof(salt_inverse));
    cuktech_crypto_zeroize(derived, sizeof(derived));
    if (result != 0) {
        cuktech_session_clear(session);
        return CUKTECH_PROTOCOL_CRYPTO_ERROR;
    }
    return CUKTECH_PROTOCOL_OK;
}

cuktech_protocol_status_t cuktech_derive_material(
    const uint8_t token[CUKTECH_TOKEN_SIZE],
    const uint8_t app_random[CUKTECH_RANDOM_SIZE],
    const uint8_t dev_random[CUKTECH_RANDOM_SIZE], uint8_t derived[64])
{
    if (token == NULL || app_random == NULL || dev_random == NULL || derived == NULL) {
        return CUKTECH_PROTOCOL_INVALID_ARGUMENT;
    }
    uint8_t salt[CUKTECH_RANDOM_SIZE * 2U];
    memcpy(salt, app_random, CUKTECH_RANDOM_SIZE);
    memcpy(salt + CUKTECH_RANDOM_SIZE, dev_random, CUKTECH_RANDOM_SIZE);
    int result = cuktech_crypto_hkdf_sha256(token, CUKTECH_TOKEN_SIZE, salt,
                                            sizeof(salt), LOGIN_INFO,
                                            sizeof(LOGIN_INFO) - 1U, derived, 64U);
    cuktech_crypto_zeroize(salt, sizeof(salt));
    return result == 0 ? CUKTECH_PROTOCOL_OK : CUKTECH_PROTOCOL_CRYPTO_ERROR;
}

void cuktech_session_clear(cuktech_session_t *session)
{
    if (session != NULL) {
        cuktech_crypto_zeroize(session, sizeof(*session));
    }
}

cuktech_protocol_status_t cuktech_encrypt_packet(cuktech_session_t *session,
                                                 const uint8_t *plaintext,
                                                 size_t plaintext_len,
                                                 uint8_t *packet,
                                                 size_t packet_capacity,
                                                 size_t *packet_len)
{
    if (session == NULL || plaintext == NULL || packet == NULL || packet_len == NULL) {
        return CUKTECH_PROTOCOL_INVALID_ARGUMENT;
    }
    if (session->send_counter >= CUKTECH_SEND_COUNTER_REKEY) {
        return CUKTECH_PROTOCOL_COUNTER_EXHAUSTED;
    }
    size_t required = 2U + plaintext_len + CUKTECH_CCM_TAG_SIZE;
    if (packet_capacity < required) {
        return CUKTECH_PROTOCOL_BUFFER_TOO_SMALL;
    }

    uint8_t counter[4];
    uint8_t nonce[CUKTECH_CCM_NONCE_SIZE] = {0};
    write_le32(counter, session->send_counter);
    memcpy(nonce, session->app_iv, CUKTECH_IV_SIZE);
    memcpy(nonce + 8U, counter, sizeof(counter));
    packet[0] = counter[0];
    packet[1] = counter[1];
    int result = cuktech_crypto_ccm_encrypt(session->app_key, nonce, plaintext,
                                            plaintext_len, packet + 2U,
                                            packet + 2U + plaintext_len);
    cuktech_crypto_zeroize(nonce, sizeof(nonce));
    cuktech_crypto_zeroize(counter, sizeof(counter));
    if (result != 0) {
        return CUKTECH_PROTOCOL_CRYPTO_ERROR;
    }
    ++session->send_counter;
    *packet_len = required;
    return CUKTECH_PROTOCOL_OK;
}

cuktech_protocol_status_t cuktech_decrypt_packet(const cuktech_session_t *session,
                                                 const uint8_t *packet,
                                                 size_t packet_len,
                                                 uint8_t *plaintext,
                                                 size_t plaintext_capacity,
                                                 size_t *plaintext_len)
{
    if (session == NULL || packet == NULL || plaintext == NULL || plaintext_len == NULL) {
        return CUKTECH_PROTOCOL_INVALID_ARGUMENT;
    }
    if (packet_len < 2U + CUKTECH_CCM_TAG_SIZE) {
        return CUKTECH_PROTOCOL_INVALID_PACKET;
    }
    size_t ciphertext_len = packet_len - 2U - CUKTECH_CCM_TAG_SIZE;
    if (plaintext_capacity < ciphertext_len) {
        return CUKTECH_PROTOCOL_BUFFER_TOO_SMALL;
    }

    uint8_t nonce[CUKTECH_CCM_NONCE_SIZE] = {0};
    memcpy(nonce, session->dev_iv, CUKTECH_IV_SIZE);
    nonce[8] = packet[0];
    nonce[9] = packet[1];
    int result = cuktech_crypto_ccm_decrypt(
        session->dev_key, nonce, packet + 2U, ciphertext_len,
        packet + 2U + ciphertext_len, plaintext);
    cuktech_crypto_zeroize(nonce, sizeof(nonce));
    if (result != 0) {
        cuktech_crypto_zeroize(plaintext, plaintext_capacity);
        return CUKTECH_PROTOCOL_AUTH_FAILED;
    }
    *plaintext_len = ciphertext_len;
    return CUKTECH_PROTOCOL_OK;
}
