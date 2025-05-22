// #include "CryptoManager.h"
// #include "KeyStore.h"
// #include <mbedtls/gcm.h>
// #include <string.h>

// using Crypto::TAG_LEN;
// static constexpr uint64_t REPLAY_WINDOW = (1ull << 24);

// static void buildNonce(uint8_t nonce[12],
//                        uint32_t packetID,
//                        uint32_t prevHopID,
//                        uint8_t type,
//                        uint32_t hi24)
// {
//     memcpy(nonce, &packetID, 4); // little-endian
//     memcpy(nonce + 4, &prevHopID, 4);
//     nonce[8] = type;
//     nonce[9] = uint8_t(hi24 & 0xFF);
//     nonce[10] = uint8_t((hi24 >> 8) & 0xFF);
//     nonce[11] = uint8_t((hi24 >> 16) & 0xFF);
// }

// /*------------------------------------------------------------------------*/
// static bool gcm(bool enc,
//                 const uint8_t key[16],
//                 const uint8_t nonce[12],
//                 const uint8_t *add, size_t addLen,
//                 uint8_t *buf, size_t len,
//                 uint8_t tag[TAG_LEN])
// {
//     mbedtls_gcm_context ctx;
//     mbedtls_gcm_init(&ctx);
//     int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
//     if (rc == 0)
//     {
//         if (enc)
//             rc = mbedtls_gcm_crypt_and_tag(&ctx,
//                                            MBEDTLS_GCM_ENCRYPT,
//                                            len,
//                                            nonce, 12,
//                                            add, addLen,
//                                            buf, buf,
//                                            TAG_LEN, tag);
//         else
//             rc = mbedtls_gcm_auth_decrypt(&ctx,
//                                           len,
//                                           nonce, 12,
//                                           add, addLen,
//                                           tag, TAG_LEN,
//                                           buf, buf);
//     }
//     mbedtls_gcm_free(&ctx);
//     return rc == 0;
// }

// /*================================  ENCRYPT  ==============================*/
// bool Crypto::encrypt(BaseHeader &hdr,
//                      const uint8_t *plain, size_t plainLen,
//                      uint8_t *out, size_t &outLen)
// {
//     /* 1. allocate/patch packetID from 64-bit counter */
//     uint64_t ctr = KeyStore::instance().nextTxCtr();
//     hdr.packetID = uint32_t(ctr); // low-32 go on air
// #if CONFIG_EXPLICIT_NONCE
//     uint16_t hi16 = uint16_t(ctr >> 32); // transmitted in NonceExt
// #endif

//     /* 2. copy plaintext into out-buffer so we can encrypt in-place */
//     memcpy(out, plain, plainLen);

//     /* 3. build implicit nonce */
//     uint8_t nonce[12];
//     buildNonce(nonce,
//                hdr.packetID,
//                hdr.prevHopID,
//                hdr.packetType,
//                uint32_t(ctr >> 32));

//     /* 4. encrypt in-place, tag after ciphertext */
//     uint8_t *tagPtr = out + plainLen;
//     if (!gcm(true, K_NET, nonce,
//              reinterpret_cast<uint8_t *>(&hdr), sizeof(BaseHeader),
//              out, plainLen,
//              tagPtr))
//         return false;

//     outLen = plainLen + TAG_LEN;
//     hdr.flags |= 0x80; // FLAG_ENCRYPTED
// #if CONFIG_EXPLICIT_NONCE
//     // caller must write hi16 into its 2-byte NonceExt field
//     (void)hi16;
// #endif
//     return true;
// }

// /*===============================  DECRYPT  ================================*/
// size_t Crypto::decrypt(const BaseHeader &hdr,
//                        uint8_t *buf, size_t len)
// {
//     if (!(hdr.flags & 0x80))
//     { // not encrypted
//         Serial.println("Not encrypted");
//         return 0;
//     }
//     if (len < TAG_LEN)
//     { // sanity
//         Serial.println("Less than tag length transmission received");
//         return 0;
//     }

//     size_t cipherLen = len - TAG_LEN;
//     uint8_t *tagPtr = buf + cipherLen;

//     /* ---------- reconstruct sender counter from packetID ------------- */
//     uint64_t pid = hdr.packetID;
//     KeyStore &ks = KeyStore::instance();
//     uint64_t last = ks.lastRxCtr(hdr.prevHopID);

//     uint64_t base = last & 0xFFFFFFFF00000000ULL;
//     uint64_t cand = base | pid;
//     if (cand <= last)
//         cand += 1ULL << 32; // wrapped

// #if CONFIG_EXPLICIT_NONCE
//     /* merge the 16 transmitted high bits held in NonceExt */
//     // uint16_t hi16 = ...  caller should have parsed them
//     // cand = (uint64_t(hi16) << 32) | pid;
// #endif

//     if (cand - last > REPLAY_WINDOW)
//     {
//         Serial.println("FAILED REPLAY WINDOW CHECK");
//         return 0; // old / way ahead
//     }

//     /* ---------- implicit nonce --------------------------------------- */
//     uint8_t nonce[12];
//     buildNonce(nonce,
//                hdr.packetID,
//                hdr.prevHopID,
//                hdr.packetType,
//                uint32_t(cand >> 32));

//     /* ---------- AEAD verify & decrypt in place ----------------------- */
//     if (!gcm(false, K_NET, nonce,
//              reinterpret_cast<const uint8_t *>(&hdr), sizeof(BaseHeader),
//              buf, cipherLen,
//              tagPtr))
//     {
//         Serial.println("GCM AUTH FAILED");
//         return 0; // auth fail
//     }

//     ks.updateRxCtr(hdr.prevHopID, cand);
//     return cipherLen; // plain length
// }
