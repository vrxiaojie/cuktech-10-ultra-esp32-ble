#pragma once

#include <stddef.h>
#include <stdint.h>

int cuktech_crypto_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                               const uint8_t *salt, size_t salt_len,
                               const uint8_t *info, size_t info_len,
                               uint8_t *output, size_t output_len);
int cuktech_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                               const uint8_t *data, size_t data_len,
                               uint8_t output[32]);
int cuktech_crypto_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                               const uint8_t *plaintext, size_t plaintext_len,
                               uint8_t *ciphertext, uint8_t tag[4]);
int cuktech_crypto_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                               const uint8_t *ciphertext, size_t ciphertext_len,
                               const uint8_t tag[4], uint8_t *plaintext);
void cuktech_crypto_zeroize(void *data, size_t length);
