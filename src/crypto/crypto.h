#pragma once
#include <stdint.h>
#include <stddef.h>

/* Network-wide 128-bit key – burn this into flash before shipment.
   Replace the hex values with your production key.                   */
extern const uint8_t NWK_KEY[16];

/*  Encrypt + authenticate.
    - nonce_len must be 12
    - tag_len   must be 8                                            */
bool aes_gcm_encrypt(const uint8_t *nonce, size_t nonce_len,
                     const uint8_t *aad,   size_t aad_len,
                     const uint8_t *plain, size_t len,
                     uint8_t *cipher,      uint8_t *tag, size_t tag_len);

/*  Decrypt + verify – returns false if the tag is wrong.             */
bool aes_gcm_decrypt(const uint8_t *nonce, size_t nonce_len,
                     const uint8_t *aad,   size_t aad_len,
                     const uint8_t *cipher, size_t len,
                     const uint8_t *tag,    size_t tag_len,
                     uint8_t *plain);
