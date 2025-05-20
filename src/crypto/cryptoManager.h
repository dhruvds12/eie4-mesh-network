// #pragma once
// #include "packet.h"

// /* Tag length is fixed for Phase-1; change in Phase-2 if needed */

// /* Set this to 1 if you decide to ship the 2-byte ‘NonceExt’ field
//  * instead of implicit high-bits reconstruction.                      */
// #ifndef CONFIG_EXPLICIT_NONCE
// #define CONFIG_EXPLICIT_NONCE 0
// #endif

// namespace Crypto
// {
//     constexpr size_t TAG_LEN = 8;

//     /*  Encrypts (extHdr||payload) into <out>.
//      *  ─ outLen = plainLen + TAG_LEN
//      *  ─ Does NOT copy any nonce to the frame – only the tag is appended. */
//     bool encrypt(BaseHeader &hdr, // will PATCH packetID
//                  const uint8_t *plain,
//                  size_t plainLen,
//                  uint8_t *out,
//                  size_t &outLen);

//     /*  Decrypts in place (buf contains cipher||tag). Returns plaintext
//      *  length or 0 on auth/replay failure. */
//     size_t decrypt(const BaseHeader &hdr,
//                    uint8_t *buf,
//                    size_t len);

// } // namespace Crypto
