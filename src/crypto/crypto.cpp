#include "crypto.h"

#ifdef UNIT_TEST
/*  Tiny-AES-GCM (public domain) – only the bits we need
    so the unit tests build without ESP32 libraries.                 */
#include "aes_gcm.h" // include your favourite minimal GCM impl
#else
#include "mbedtls/gcm.h"
#endif

// Probably shouldn't be on github -- change completely and store locally
const uint8_t NWK_KEY[16] =
    {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

static bool gcm(bool encrypt,
                const uint8_t *nonce, size_t nonce_len,
                const uint8_t *aad, size_t aad_len,
                const uint8_t *input, size_t len,
                uint8_t *output,
                const uint8_t *tag_in, uint8_t *tag_out, size_t tag_len)
{
#ifdef UNIT_TEST
    /* your Tiny-AES-GCM wrapper here */
    (void)encrypt;
    (void)nonce;
    (void)nonce_len;
    (void)aad;
    (void)aad_len;
    (void)input;
    (void)len;
    (void)output;
    (void)tag_in;
    (void)tag_out;
    (void)tag_len;
    /* just do a dumb mem-copy for tests */
    if (encrypt)
        memcpy(output, input, len);
    else
        memcpy(output, input, len);
    if (tag_out)
        memset(tag_out, 0, tag_len);
    return true;
#else
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                                NWK_KEY, 128);
    if (rc != 0)
    {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    if (encrypt)
        rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT,
                                       len,
                                       nonce, nonce_len,
                                       aad, aad_len,
                                       input, output,
                                       tag_len, tag_out);
    else
        rc = mbedtls_gcm_auth_decrypt(&ctx,
                                      len,
                                      nonce, nonce_len,
                                      aad, aad_len,
                                      tag_in, tag_len,
                                      input, output);

    mbedtls_gcm_free(&ctx);
    return rc == 0;
#endif
}

bool aes_gcm_encrypt(const uint8_t *nonce, size_t nonce_len,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *plain, size_t len,
                     uint8_t *cipher, uint8_t *tag, size_t tag_len)
{
    return gcm(true, nonce, nonce_len, aad, aad_len,
               plain, len, cipher, nullptr, tag, tag_len);
}

bool aes_gcm_decrypt(const uint8_t *nonce, size_t nonce_len,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *cipher, size_t len,
                     const uint8_t *tag, size_t tag_len,
                     uint8_t *plain)
{
    return gcm(false, nonce, nonce_len, aad, aad_len,
               cipher, len, plain, tag, nullptr, tag_len);
}
