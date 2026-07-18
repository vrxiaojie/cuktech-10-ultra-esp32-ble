#include "crypto_backend.h"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

int cuktech_crypto_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                               const uint8_t *salt, size_t salt_len,
                               const uint8_t *info, size_t info_len,
                               uint8_t *output, size_t output_len)
{
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (kdf == NULL) {
        return -1;
    }
    EVP_KDF_CTX *context = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (context == NULL) {
        return -1;
    }
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, "SHA256", 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)ikm, ikm_len),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void *)salt, salt_len),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (void *)info, info_len),
        OSSL_PARAM_construct_end(),
    };
    int result = EVP_KDF_derive(context, output, output_len, params) == 1 ? 0 : -1;
    EVP_KDF_CTX_free(context);
    return result;
}

int cuktech_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                               const uint8_t *data, size_t data_len,
                               uint8_t output[32])
{
    size_t output_len = 0;
    unsigned char *result = EVP_Q_mac(NULL, "HMAC", NULL, "SHA256", NULL, key,
                                      key_len, data, data_len, output, 32U,
                                      &output_len);
    return result != NULL && output_len == 32U ? 0 : -1;
}

int cuktech_crypto_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                               const uint8_t *plaintext, size_t plaintext_len,
                               uint8_t *ciphertext, uint8_t tag[4])
{
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (context == NULL) {
        return -1;
    }
    int length = 0;
    int result = EVP_EncryptInit_ex(context, EVP_aes_128_ccm(), NULL, NULL, NULL) == 1 &&
                         EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_SET_IVLEN, 12, NULL) == 1 &&
                         EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_SET_TAG, 4, NULL) == 1 &&
                         EVP_EncryptInit_ex(context, NULL, NULL, key, nonce) == 1 &&
                         EVP_EncryptUpdate(context, NULL, &length, NULL,
                                           (int)plaintext_len) == 1 &&
                         EVP_EncryptUpdate(context, ciphertext, &length, plaintext,
                                           (int)plaintext_len) == 1 &&
                         EVP_EncryptFinal_ex(context, ciphertext + length, &length) == 1 &&
                         EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_GET_TAG, 4, tag) == 1
                     ? 0
                     : -1;
    EVP_CIPHER_CTX_free(context);
    return result;
}

int cuktech_crypto_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                               const uint8_t *ciphertext, size_t ciphertext_len,
                               const uint8_t tag[4], uint8_t *plaintext)
{
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (context == NULL) {
        return -1;
    }
    int length = 0;
    int result = EVP_DecryptInit_ex(context, EVP_aes_128_ccm(), NULL, NULL, NULL) == 1 &&
                         EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_SET_IVLEN, 12, NULL) == 1 &&
                         EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_CCM_SET_TAG, 4,
                                             (void *)tag) == 1 &&
                         EVP_DecryptInit_ex(context, NULL, NULL, key, nonce) == 1 &&
                         EVP_DecryptUpdate(context, NULL, &length, NULL,
                                           (int)ciphertext_len) == 1 &&
                         EVP_DecryptUpdate(context, plaintext, &length, ciphertext,
                                           (int)ciphertext_len) == 1
                     ? 0
                     : -1;
    EVP_CIPHER_CTX_free(context);
    return result;
}

void cuktech_crypto_zeroize(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (length-- > 0U) {
        *bytes++ = 0U;
    }
}
