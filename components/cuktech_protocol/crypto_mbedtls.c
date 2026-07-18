#include "crypto_backend.h"

#include "mbedtls/ccm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

int cuktech_crypto_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                               const uint8_t *salt, size_t salt_len,
                               const uint8_t *info, size_t info_len,
                               uint8_t *output, size_t output_len)
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md == NULL ? -1
                      : mbedtls_hkdf(md, salt, salt_len, ikm, ikm_len, info,
                                     info_len, output, output_len);
}

int cuktech_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                               const uint8_t *data, size_t data_len,
                               uint8_t output[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md == NULL ? -1 : mbedtls_md_hmac(md, key, key_len, data, data_len, output);
}

int cuktech_crypto_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                               const uint8_t *plaintext, size_t plaintext_len,
                               uint8_t *ciphertext, uint8_t tag[4])
{
    mbedtls_ccm_context context;
    mbedtls_ccm_init(&context);
    int result = mbedtls_ccm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 128U);
    if (result == 0) {
        result = mbedtls_ccm_encrypt_and_tag(&context, plaintext_len, nonce, 12U,
                                             NULL, 0U, plaintext, ciphertext, tag,
                                             4U);
    }
    mbedtls_ccm_free(&context);
    return result;
}

int cuktech_crypto_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                               const uint8_t *ciphertext, size_t ciphertext_len,
                               const uint8_t tag[4], uint8_t *plaintext)
{
    mbedtls_ccm_context context;
    mbedtls_ccm_init(&context);
    int result = mbedtls_ccm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 128U);
    if (result == 0) {
        result = mbedtls_ccm_auth_decrypt(&context, ciphertext_len, nonce, 12U,
                                          NULL, 0U, ciphertext, plaintext, tag, 4U);
    }
    mbedtls_ccm_free(&context);
    return result;
}

void cuktech_crypto_zeroize(void *data, size_t length)
{
    mbedtls_platform_zeroize(data, length);
}
